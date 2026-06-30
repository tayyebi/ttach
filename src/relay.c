#include "relay.h"
#include "pty.h"
#include "signal.h"

#include <poll.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>

static int write_all(int fd, const char *buf, int len) {
    int off = 0, n;

    while (off < len) {
        n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        off += n;
    }
    return off;
}

int relay_loop(int pty_fd, int client_fd, struct winsize *ws) {
    struct pollfd fds[2];
    char buf[65536];
    unsigned char wsize[4];
    int n;

    n = read(client_fd, wsize, 4);
    if (n == 4) {
        ws->ws_row = (wsize[0] << 8) | wsize[1];
        ws->ws_col = (wsize[2] << 8) | wsize[3];
        if (ws->ws_row > 0 && ws->ws_col > 0)
            pty_update_size(pty_fd, ws);
    }

    fds[0].fd = pty_fd;
    fds[0].events = POLLIN;
    fds[1].fd = client_fd;
    fds[1].events = POLLIN;

    while (1) {
        if (tt_sigchld || tt_sigterm)
            break;

        if (tt_sigwinch) {
            tt_sigwinch = 0;
            if (ioctl(STDIN_FILENO, TIOCGWINSZ, ws) == 0)
                pty_update_size(pty_fd, ws);
        }

        fds[0].revents = 0;
        fds[1].revents = 0;

        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        if (fds[0].revents & POLLIN) {
            n = read(pty_fd, buf, sizeof(buf));
            if (n <= 0) break;
            if (write_all(client_fd, buf, n) < 0) break;
        }

        if (fds[1].revents & POLLIN) {
            n = read(client_fd, buf, sizeof(buf));
            if (n <= 0) break;
            if (write_all(pty_fd, buf, n) < 0) break;
        }

        if (fds[0].revents & (POLLHUP | POLLERR)) break;
        if (fds[1].revents & (POLLHUP | POLLERR)) break;
    }

    return 0;
}
