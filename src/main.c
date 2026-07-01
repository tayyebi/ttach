#include "server.h"
#include "client.h"
#include "socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

int main(void) {
    int fd;

    fd = socket_connect();
    if (fd >= 0) {
        client_run(fd);
        return 0;
    }

    if (!isatty(STDIN_FILENO)) {
        server_run();
        return 0;
    }

    signal(SIGCHLD, SIG_IGN);

    pid_t server_pid = fork();
    if (server_pid < 0) {
        server_run();
        return 0;
    }

    if (server_pid == 0) {
        setsid();

        int nullfd = open("/dev/null", O_RDWR);
        if (nullfd >= 0) {
            dup2(nullfd, STDIN_FILENO);
            dup2(nullfd, STDOUT_FILENO);
            dup2(nullfd, STDERR_FILENO);
            if (nullfd > 2) close(nullfd);
        }

        server_run();
        _exit(0);
    }

    for (int i = 0; i < 50; i++) {
        fd = socket_connect();
        if (fd >= 0) {
            setenv("TTACH_SESSION", "1", 1);
            client_run(fd);
            return 0;
        }
        if (kill(server_pid, 0) < 0)
            break;
        usleep(100000);
    }

    fprintf(stderr, "ttach: server failed to start\n");
    return 1;
}
