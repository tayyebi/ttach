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

static void make_path(void) {
    const char *dir = getenv("XDG_RUNTIME_DIR");
    uid_t uid = getuid();
    if (dir && dir[0])
        snprintf(socket_path, sizeof(socket_path), "%s/ttach.sock", dir);
    else
        snprintf(socket_path, sizeof(socket_path), "/tmp/ttach-%d.sock", uid);
}

static void msleep(int ms) {
    poll(NULL, 0, ms);
}

static int start_server(const char *binary) {
    make_path();
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

    for (int i = 0; i < 50; i++) {
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

    for (int i = 0; i < 5; i++) {
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            return fd;
        msleep(300);
    }
    close(fd);
    return -1;
}

static int write_all(int fd, const char *buf, int len) {
    int off = 0;
    while (off < len) {
        int n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += n;
    }
    return off;
}

static int read_until(int fd, char *buf, int bufsz,
                      const char *expect, int timeout_ms)
{
    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    int total = 0;
    int max_iter = timeout_ms / 100;

    for (int i = 0; i < max_iter; i++) {
        if (total >= bufsz - 1) break;

        int ret = poll(&pfd, 1, 100);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;

        int n = read(fd, buf + total, bufsz - 1 - total);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';
        if (expect && strstr(buf, expect))
            break;
    }
    if (total < bufsz) buf[total] = '\0';
    return total;
}

#define TEST(name, expr) do { \
    if (expr) { passed++; printf("  PASS: %s\n", name); } \
    else { failed++; printf("  FAIL: %s\n", name); } \
} while (0)

int main(int argc, char **argv) {
    const char *binary = argc > 1 ? argv[1] : "./ttach";
    char buf[65536];
    unsigned char handshake[4] = {0, 24, 0, 80};

    printf("ttach test suite\n");
    printf("----------------\n");

    if (start_server(binary) < 0) {
        fprintf(stderr, "FAIL: could not start server\n");
        return 1;
    }

    /* Test 1: socket creation */
    TEST("socket creation", access(socket_path, F_OK) == 0);

    /* Test 2: client relay */
    int ok = 0;
    int s = connect_socket();
    if (s >= 0) {
        if (write_all(s, (char *)handshake, 4) == 4) {
            write_all(s, "echo TTACH_TEST_OK\n", 19);
            int n = read_until(s, buf, sizeof(buf), "TTACH_TEST_OK", 5000);
            ok = (n > 0 && strstr(buf, "TTACH_TEST_OK"));
        }
        close(s);
    }
    TEST("client relay: data forwarded through PTY", ok);

    /* Test 3: reconnect */
    ok = 0;
    s = connect_socket();
    if (s >= 0) {
        write_all(s, (char *)handshake, 4);
        write_all(s, "TTACH_RECONNECT=yes\n", 20);
        msleep(500);
        close(s);
        msleep(300);

        s = connect_socket();
        if (s >= 0) {
            write_all(s, (char *)handshake, 4);
            write_all(s, "echo $TTACH_RECONNECT\n", 22);
            int n = read_until(s, buf, sizeof(buf), "yes", 5000);
            ok = (n > 0 && strstr(buf, "yes"));
            close(s);
        }
    }
    TEST("reconnect: shell state preserved", ok);

    /* Test 4: multi-cycle */
    ok = 1;
    for (int i = 0; i < 3; i++) {
        s = connect_socket();
        if (s < 0) { ok = 0; break; }
        write_all(s, (char *)handshake, 4);
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "echo CYCLE_%d\n", i);
        write_all(s, cmd, strlen(cmd));
        char expect[32];
        snprintf(expect, sizeof(expect), "CYCLE_%d", i);
        int n = read_until(s, buf, sizeof(buf), expect, 5000);
        if (n <= 0 || !strstr(buf, expect)) { ok = 0; close(s); break; }
        close(s);
        msleep(300);
    }
    TEST("multi-cycle: 3 disconnect/reconnect cycles", ok);

    /* Test 5: sigterm cleanup */
    kill(server_pid, SIGTERM);
    waitpid(server_pid, NULL, 0);
    server_pid = -1;
    int cleaned = 0;
    for (int i = 0; i < 10; i++) {
        if (access(socket_path, F_OK) != 0) { cleaned = 1; break; }
        msleep(300);
    }
    TEST("sigterm cleanup: socket removed", cleaned);

    printf("----------------\n");
    printf("%d passed, %d failed\n", passed, failed);

    stop_server();
    return failed == 0 ? 0 : 1;
}
