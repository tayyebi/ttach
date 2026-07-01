#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>
#include <pwd.h>
#include <fcntl.h>

static char socket_path[256];
static pid_t server_pid = -1;
static int passed = 0;
static int failed = 0;

static void make_path_default(void) {
    const char *dir = getenv("XDG_RUNTIME_DIR");
    uid_t uid = getuid();
    if (dir && dir[0])
        snprintf(socket_path, sizeof(socket_path), "%s/ttach.sock", dir);
    else
        snprintf(socket_path, sizeof(socket_path), "/tmp/ttach-%d.sock", uid);
}

static void msleep(int ms) { poll(NULL, 0, ms); }

static int start_server(const char *binary) {
    make_path_default();
    unlink(socket_path);

    server_pid = fork();
    if (server_pid < 0) return -1;
    if (server_pid == 0) {
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2) close(fd);
        }
        execlp(binary, binary, (char *)NULL);
        _exit(1);
    }

    for (int i = 0; i < 80; i++) {
        if (access(socket_path, F_OK) == 0) return 0;
        msleep(100);
    }
    return -1;
}

static void stop_server(void) {
    if (server_pid > 0) {
        kill(server_pid, SIGTERM);
        waitpid(server_pid, NULL, 0);
        server_pid = -1;
    }
    unlink(socket_path);
}

static int connect_socket(void) {
    struct sockaddr_un addr;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    for (int i = 0; i < 20; i++) {
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            return fd;
        msleep(150);
    }
    close(fd);
    return -1;
}

static int try_connect(void) {
    struct sockaddr_un addr;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int write_all(int fd, const char *buf, int len) {
    int off = 0;
    while (off < len) {
        int n = write(fd, buf + off, len - off);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        off += n;
    }
    return off;
}

static int read_until(int fd, char *buf, int bufsz,
                      const char *expect, int timeout_ms)
{
    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    int total = 0, max_iter = timeout_ms / 50;

    for (int i = 0; i < max_iter; i++) {
        if (total >= bufsz - 1) break;
        int ret = poll(&pfd, 1, 50);
        if (ret < 0) { if (errno == EINTR) continue; break; }
        if (ret == 0) continue;
        int n = read(fd, buf + total, bufsz - 1 - total);
        if (n <= 0) break;
        total += n; buf[total] = '\0';
        if (expect && strstr(buf, expect)) break;
    }
    if (total < bufsz) buf[total] = '\0';
    return total;
}

static unsigned char hshake_24_80[4] = {0, 24, 0, 80};

static int do_roundtrip(const char *cmd, const char *expect) {
    char buf[65536] = {0};
    int s = connect_socket();
    if (s < 0) return 0;
    if (write_all(s, (char *)hshake_24_80, 4) != 4) { close(s); return 0; }
    msleep(300);
    if (cmd && cmd[0]) {
        write_all(s, cmd, strlen(cmd));
        write_all(s, "\n", 1);
    }
    int n = read_until(s, buf, sizeof(buf), expect, 5000);
    close(s);
    msleep(500);
    return (expect && expect[0]) ? (n > 0 && strstr(buf, expect)) : 1;
}

static int roundtrip_sized(const char *cmd, const char *expect,
                           unsigned short rows, unsigned short cols)
{
    char buf[65536] = {0};
    unsigned char hs[4];
    hs[0] = (rows >> 8) & 0xff; hs[1] = rows & 0xff;
    hs[2] = (cols >> 8) & 0xff; hs[3] = cols & 0xff;

    int s = connect_socket();
    if (s < 0) return 0;
    if (write_all(s, (char *)hs, 4) != 4) { close(s); return 0; }
    msleep(300);
    write_all(s, cmd, strlen(cmd));
    write_all(s, "\n", 1);
    int n = read_until(s, buf, sizeof(buf), expect, 5000);
    close(s);
    msleep(500);
    return (n > 0 && strstr(buf, expect));
}

#define TEST(name, expr) do { \
    if (expr) { passed++; printf("\033[32m  PASS\033[0m: %s\n", name); } \
    else { failed++; printf("\033[31m  FAIL\033[0m: %s\n", name); } \
} while (0)

static void srv_start_or_die(const char *binary, const char *label) {
    if (start_server(binary) < 0) {
        fprintf(stderr, "FATAL: %s - could not start server\n", label);
        exit(1);
    }
}

int main(int argc, char **argv) {
    const char *binary = argc > 1 ? argv[1] : "./ttach";
    signal(SIGPIPE, SIG_IGN);

    printf("ttach test suite\n================\n");

    /* ---------- startup & socket path ---------- */
    fprintf(stderr, "\n--- startup & socket path ---\n");
    srv_start_or_die(binary, "startup");
    TEST("socket created", access(socket_path, F_OK) == 0);

    /* ---------- basic relay ---------- */
    fprintf(stderr, "\n--- basic relay ---\n");
    TEST("command relayed through PTY",
         do_roundtrip("echo RELAY_OK", "RELAY_OK"));

    /* ---------- reconnect ---------- */
    fprintf(stderr, "\n--- reconnect & state preservation ---\n");
    {
        int s = connect_socket();
        int ok = 0;
        if (s >= 0) {
            write_all(s, (char *)hshake_24_80, 4);
            write_all(s, "TTACH_RECONNECT_VAR=yes\n", 24);
            msleep(500);
            close(s);
            msleep(800);
            s = connect_socket();
            if (s >= 0) {
                write_all(s, (char *)hshake_24_80, 4);
                write_all(s, "echo $TTACH_RECONNECT_VAR\n", 26);
                char buf[65536] = {0};
                int n = read_until(s, buf, sizeof(buf), "yes", 5000);
                ok = (n > 0 && strstr(buf, "yes"));
                close(s);
            }
        }
        TEST("shell retains env across reconnect", ok);
    }

    /* ---------- multi-cycle ---------- */
    fprintf(stderr, "\n--- multi-cycle ---\n");
    {
        int ok = 1;
        for (int i = 0; i < 3; i++) {
            int s = connect_socket();
            if (s < 0) { ok = 0; break; }
            write_all(s, (char *)hshake_24_80, 4);
            msleep(300);
            char cmd[64], exp[32];
            snprintf(cmd, sizeof(cmd), "echo CYCLE_%d\n", i);
            snprintf(exp, sizeof(exp), "CYCLE_%d", i);
            write_all(s, cmd, strlen(cmd));
            char buf[65536] = {0};
            int n = read_until(s, buf, sizeof(buf), exp, 5000);
            if (n <= 0 || !strstr(buf, exp)) { ok = 0; close(s); break; }
            close(s);
            msleep(800);
        }
        TEST("3 disconnect/reconnect cycles", ok);
    }

    /* ---------- terminal sizes ---------- */
    fprintf(stderr, "\n--- terminal sizes ---\n");
    msleep(500);
    TEST("24x80",
         roundtrip_sized("echo SZ_24x80", "SZ_24x80", 24, 80));
    TEST("40x120",
         roundtrip_sized("echo SZ_40x120", "SZ_40x120", 40, 120));
    TEST("60x200",
         roundtrip_sized("echo SZ_60x200", "SZ_60x200", 60, 200));

    stop_server();
    msleep(500);

    /* ---------- XDG_RUNTIME_DIR fallback ---------- */
    fprintf(stderr, "\n--- XDG_RUNTIME_DIR fallback ---\n");
    unsetenv("XDG_RUNTIME_DIR");
    srv_start_or_die(binary, "xdg-fallback");
    TEST("server starts without XDG_RUNTIME_DIR",
         access(socket_path, F_OK) == 0);
    TEST("relay via /tmp fallback socket",
         do_roundtrip("echo XDG_FALLBACK", "XDG_FALLBACK"));
    stop_server();
    msleep(500);

    /* ---------- SIGTERM cleanup ---------- */
    fprintf(stderr, "\n--- SIGTERM cleanup ---\n");
    setenv("XDG_RUNTIME_DIR", "", 1);
    srv_start_or_die(binary, "sigterm");
    TEST("alive before SIGTERM",
         do_roundtrip("echo ALIVE", "ALIVE"));
    kill(server_pid, SIGTERM);
    waitpid(server_pid, NULL, 0);
    server_pid = -1;
    {
        int cleaned = 0;
        for (int i = 0; i < 15; i++) {
            if (access(socket_path, F_OK) != 0) { cleaned = 1; break; }
            msleep(200);
        }
        TEST("socket removed after SIGTERM", cleaned);
    }

    /* ---------- SIGCHLD / shell exit ---------- */
    fprintf(stderr, "\n--- SIGCHLD on shell exit ---\n");
    unlink(socket_path);
    srv_start_or_die(binary, "sigchld");
    {
        int s = connect_socket();
        if (s >= 0) {
            write_all(s, (char *)hshake_24_80, 4);
            write_all(s, "exit\n", 5);
            msleep(800);
            close(s);
        }
        msleep(1500);
        int dead = (access(socket_path, F_OK) != 0);
        TEST("server exits and cleans up when shell exits", dead);
    }
    if (server_pid > 0) { stop_server(); }

    /* ---------- rapid connect/disconnect ---------- */
    fprintf(stderr, "\n--- rapid connect/disconnect ---\n");
    unlink(socket_path);
    srv_start_or_die(binary, "rapid");
    {
        int rapid_ok = 1;
        for (int i = 0; i < 10; i++) {
            int s = connect_socket();
            if (s < 0) { rapid_ok = 0; break; }
            write_all(s, (char *)hshake_24_80, 4);
            char cmd[64], exp[32];
            snprintf(cmd, sizeof(cmd), "echo RAPID_%d\n", i);
            snprintf(exp, sizeof(exp), "RAPID_%d", i);
            write_all(s, cmd, strlen(cmd));
            char buf[65536] = {0};
            int n = read_until(s, buf, sizeof(buf), exp, 5000);
            if (n <= 0 || !strstr(buf, exp)) { rapid_ok = 0; close(s); break; }
            close(s);
            msleep(300);
        }
        TEST("10 rapid connect/disconnect cycles", rapid_ok);
    }
    stop_server();
    msleep(800);

    /* ---------- backlog / double-connect ---------- */
    fprintf(stderr, "\n--- backlog ---\n");
    srv_start_or_die(binary, "backlog");
    {
        int s1 = try_connect();
        int s2 = try_connect();
        TEST("first connect OK", s1 >= 0);
        TEST("second connect queued (backlog=1)", s2 >= 0);
        if (s1 >= 0) close(s1);
        if (s2 >= 0) close(s2);
        msleep(1000);
        TEST("relay works after draining backlog",
             do_roundtrip("echo AFTER_BACKLOG", "AFTER_BACKLOG"));
    }
    stop_server();

    msleep(500);
    unlink(socket_path);

    printf("================\n");
    printf("%d passed, %d failed\n", passed, failed);
    stop_server();
    return failed == 0 ? 0 : 1;
}
