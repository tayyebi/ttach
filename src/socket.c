#include "socket.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>

static char socket_path[256];
static int sock_fd = -1;

static void make_path(void) {
    const char *dir;
    uid_t uid = getuid();

    dir = getenv("XDG_RUNTIME_DIR");
    if (dir && *dir) {
        snprintf(socket_path, sizeof(socket_path),
                 "%s/ttach.sock", dir);
    } else {
        snprintf(socket_path, sizeof(socket_path),
                 "/tmp/ttach-%d.sock", uid);
    }
}

int socket_create(void) {
    struct sockaddr_un addr;

    make_path();
    unlink(socket_path);

    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0)
        return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        goto fail;

    if (listen(sock_fd, 1) < 0)
        goto fail;

    return sock_fd;

fail:
    close(sock_fd);
    sock_fd = -1;
    unlink(socket_path);
    return -1;
}

int socket_accept(int fd) {
    struct sockaddr_un addr;
    socklen_t len = sizeof(addr);
    return accept(fd, (struct sockaddr *)&addr, &len);
}

int socket_connect(void) {
    struct sockaddr_un addr;
    int fd;

    make_path();

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

void socket_cleanup(void) {
    if (sock_fd >= 0) {
        close(sock_fd);
        sock_fd = -1;
    }
    if (socket_path[0])
        unlink(socket_path);
}
