#!/bin/sh
set -e

BINARY="${1:-./ttach}"
fail=0
pass=0

pass_test() { pass=$((pass + 1)); echo "  PASS: $1"; }
fail_test() { fail=$((fail + 1)); echo "  FAIL: $1"; }

cleanup() {
    kill $(pgrep ttach 2>/dev/null) 2>/dev/null || true
    rm -f /run/user/0/ttach.sock /tmp/ttach-*.sock /tmp/ttach-integration-*.out
}
trap cleanup EXIT

echo "ttach integration tests"
echo "========================"
cleanup

# ----- test: auto-fork interactive mode (TTY stdin) -----
echo ""
echo "--- auto-fork interactive (PTY) ---"
printf 'echo INTERACTIVE_OK\nexit\n' | script -q -c "$BINARY" /tmp/ttach-integration-$$.out >/dev/null 2>&1
if grep -q "INTERACTIVE_OK" /tmp/ttach-integration-$$.out 2>/dev/null; then
    pass_test "single invocation in PTY auto-forks server + connects as client"
else
    fail_test "single invocation in PTY auto-forks server + connects as client"
fi
cleanup

# ----- test: cleanup after interactive exit -----
echo ""
echo "--- cleanup ---"
if pgrep ttach >/dev/null 2>&1; then
    fail_test "no leftover ttach processes after shell exit"
else
    pass_test "no leftover ttach processes after shell exit"
fi
if [ -S /run/user/0/ttach.sock ] || ls /tmp/ttach-*.sock >/dev/null 2>&1; then
    fail_test "no leftover ttach sockets after shell exit"
else
    pass_test "no leftover ttach sockets after shell exit"
fi

# ----- test: background server with Python client -----
echo ""
echo "--- background server raw-socket relay ---"
cleanup
nohup "$BINARY" </dev/null >/dev/null 2>&1 &
SRV_PID=$!
sleep 1

SOCK=$(ls /run/user/0/ttach.sock /tmp/ttach-*.sock 2>/dev/null | head -1)
if [ -z "$SOCK" ]; then
    fail_test "background server creates socket"
else
    pass_test "background server creates socket"

    python3 -c "
import socket, struct, time
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(5)
s.connect('$SOCK')
s.sendall(struct.pack('!HH', 24, 80))
time.sleep(0.3)
s.sendall(b'echo BG_SRV_OK\n')
data = b''
deadline = time.time() + 5
while time.time() < deadline:
    try:
        c = s.recv(4096)
        if not c: break
        data += c
        if b'BG_SRV_OK' in data: break
    except socket.timeout: break
s.close()
assert b'BG_SRV_OK' in data
"
    if [ $? -eq 0 ]; then
        pass_test "background server relays command via raw socket"
    else
        fail_test "background server relays command via raw socket"
    fi
fi

kill $SRV_PID 2>/dev/null || true
wait $SRV_PID 2>/dev/null || true

# ----- test: reconnect state preservation -----
echo ""
echo "--- reconnect state preservation ---"
cleanup
nohup "$BINARY" </dev/null >/dev/null 2>&1 &
SRV_PID=$!
sleep 1

SOCK=$(ls /run/user/0/ttach.sock /tmp/ttach-*.sock 2>/dev/null | head -1)
if [ -z "$SOCK" ]; then
    fail_test "server started for reconnect test"
else
    python3 -c "
import socket, struct, time

def raw_cmd(sock_path, cmd_bytes, expect, timeout=5):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(sock_path)
    s.sendall(struct.pack('!HH', 24, 80))
    time.sleep(0.3)
    s.sendall(cmd_bytes)
    data = b''
    dl = time.time() + timeout
    while time.time() < dl:
        try:
            c = s.recv(4096)
            if not c: break
            data += c
            if expect in data: break
        except socket.timeout: break
    s.close()
    return data

raw_cmd('$SOCK', b'ITEST_VAR=reconnected\n', b'')
time.sleep(0.5)
data = raw_cmd('$SOCK', b'echo \$ITEST_VAR\n', b'reconnected', timeout=8)
assert b'reconnected' in data, f'not found in: {data!r}'
"
    if [ $? -eq 0 ]; then
        pass_test "shell state preserved across disconnect/reconnect"
    else
        fail_test "shell state preserved across disconnect/reconnect"
    fi
fi

kill $SRV_PID 2>/dev/null || true
wait $SRV_PID 2>/dev/null || true

# ----- results -----
echo ""
echo "========================"
echo "$pass passed, $fail failed"
exit $fail
