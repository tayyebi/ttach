#include "client.h"

#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <errno.h>

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

void client_run(int fd) {
    struct winsize ws;
    struct termios old, new;
    struct pollfd fds[2];
    char buf[65536];
    unsigned char wsize[4];

    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        wsize[0] = (ws.ws_row >> 8) & 0xff;
        wsize[1] = ws.ws_row & 0xff;
        wsize[2] = (ws.ws_col >> 8) & 0xff;
        wsize[3] = ws.ws_col & 0xff;
    } else {
        wsize[0] = 0; wsize[1] = 24;
        wsize[2] = 0; wsize[3] = 80;
    }
    write_all(fd, (char *)wsize, 4);

    tcgetattr(STDIN_FILENO, &old);
    new = old;
    cfmakeraw(&new);
    tcsetattr(STDIN_FILENO, TCSANOW, &new);

    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = fd;
    fds[1].events = POLLIN;

    while (1) {
        fds[0].revents = 0;
        fds[1].revents = 0;

        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        if (fds[0].revents & POLLIN) {
            int n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) break;
            if (write_all(fd, buf, n) < 0) break;
        }

        if (fds[1].revents & POLLIN) {
            int n = read(fd, buf, sizeof(buf));
            if (n <= 0) break;
            if (write_all(STDOUT_FILENO, buf, n) < 0) break;
        }

        if (fds[0].revents & (POLLHUP | POLLERR)) break;
        if (fds[1].revents & (POLLHUP | POLLERR)) break;
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    close(fd);
}
