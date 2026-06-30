#!/usr/bin/env python3
import os, socket, struct, subprocess, sys, time

SOCKET_CANDIDATES = []
xdg = os.environ.get("XDG_RUNTIME_DIR")
if xdg:
    SOCKET_CANDIDATES.append(os.path.join(xdg, "ttach.sock"))
SOCKET_CANDIDATES.append("/tmp/ttach-%d.sock" % os.getuid())


class TtachTest:
    def __init__(self, binary="./ttach"):
        self.binary = binary
        self.server = None
        self.socket_path = None
        self.passed = 0
        self.failed = 0

    def _find_socket(self):
        for p in SOCKET_CANDIDATES:
            if os.path.exists(p):
                return p
        return SOCKET_CANDIDATES[-1]

    def _ok(self, msg):
        self.passed += 1
        print("  \033[92mPASS\033[0m: %s" % msg)

    def _fail(self, msg, detail=""):
        self.failed += 1
        print("  \033[91mFAIL\033[0m: %s %s" % (msg, detail))

    def start_server(self):
        self.server = subprocess.Popen(
            [self.binary],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        deadline = time.time() + 5
        while time.time() < deadline:
            p = self._find_socket()
            if os.path.exists(p):
                self.socket_path = p
                return True
            time.sleep(0.1)
        self._fail("start_server", "(socket never appeared)")
        return False

    def stop_server(self):
        if self.server:
            try:
                self.server.terminate()
                self.server.wait(timeout=3)
            except Exception:
                self.server.kill()
                self.server.wait()
            self.server = None
        if self.socket_path and os.path.exists(self.socket_path):
            try:
                os.unlink(self.socket_path)
            except OSError:
                pass

    def _connect(self, retries=5):
        last_err = None
        for attempt in range(retries):
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.settimeout(5)
                s.connect(self.socket_path)
                s.sendall(struct.pack(">HH", 24, 80))
                return s
            except socket.error as e:
                last_err = e
                time.sleep(0.3)
        raise last_err

    def _send_expect(self, sock, data, expect, timeout=5):
        sock.sendall(data)
        result = b""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                result += chunk
                if isinstance(expect, bytes) and expect in result:
                    return result
            except socket.timeout:
                break
        return result

    def _send_wait(self, sock, data, timeout=1):
        sock.sendall(data)
        if timeout > 0:
            time.sleep(timeout)

    def test_socket_creation(self):
        ok = self.socket_path and os.path.exists(self.socket_path)
        if ok:
            self._ok("socket created at %s" % self.socket_path)
        else:
            self._fail("socket not created")
        return ok

    def test_client_relay(self):
        try:
            s = self._connect()
            r = self._send_expect(s, b"echo TTACH_TEST_OK\n", b"TTACH_TEST_OK")
            s.close()
            if r and b"TTACH_TEST_OK" in r:
                self._ok("client relay: data forwarded through PTY")
                return True
            self._fail("client relay", "(expected TTACH_TEST_OK in output)")
            return False
        except Exception as e:
            self._fail("client relay", "(%s)" % e)
            return False

    def test_reconnect(self):
        try:
            s1 = self._connect()
            self._send_wait(s1, b"TTACH_RECONNECT=yes\n", timeout=0.5)
            s1.close()
            time.sleep(0.3)
            s2 = self._connect()
            r = self._send_expect(s2, b"echo $TTACH_RECONNECT\n", b"yes")
            s2.close()
            if r and b"yes" in r:
                self._ok("reconnect: shell state preserved")
                return True
            self._fail("reconnect", "(expected 'yes' in output)")
            return False
        except Exception as e:
            self._fail("reconnect", "(%s)" % e)
            return False

    def test_multi_cycle(self):
        try:
            for i in range(3):
                s = self._connect()
                r = self._send_expect(
                    s, b"echo CYCLE_%d\n" % i, b"CYCLE_%d" % i
                )
                s.close()
                if not r or ("CYCLE_%d" % i).encode() not in r:
                    self._fail("multi-cycle", "(cycle %d failed)" % i)
                    return False
                time.sleep(0.3)
            self._ok("multi-cycle: 3 disconnect/reconnect cycles")
            return True
        except Exception as e:
            self._fail("multi-cycle", "(%s)" % e)
            return False

    def test_sigterm_cleanup(self):
        try:
            p = self.socket_path
            self.server.terminate()
            self.server.wait(timeout=3)
            self.server = None
            for _ in range(10):
                if not os.path.exists(p):
                    break
                time.sleep(0.3)
            if os.path.exists(p):
                self._fail("sigterm cleanup", "(socket still exists)")
                return False
            self._ok("sigterm cleanup: socket removed")
            return True
        except Exception as e:
            self._fail("sigterm cleanup", "(%s)" % e)
            return False

    def run(self):
        print("ttach test suite")
        print("----------------")
        tests = [
            ("socket creation", self.test_socket_creation),
            ("client relay", self.test_client_relay),
            ("reconnect", self.test_reconnect),
            ("multi-cycle", self.test_multi_cycle),
            ("sigterm cleanup", self.test_sigterm_cleanup),
        ]
        for name, fn in tests:
            fn()
        print("----------------")
        print("%d passed, %d failed" % (self.passed, self.failed))
        return self.failed == 0


if __name__ == "__main__":
    t = TtachTest()
    ok = False
    try:
        if not t.start_server():
            sys.exit(1)
        ok = t.run()
    finally:
        t.stop_server()
    sys.exit(0 if ok else 1)
