#include "server.h"
#include "pty.h"
#include "socket.h"
#include "relay.h"
#include "signal.h"

#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

void server_run(void) {
    struct winsize ws;
    pid_t child_pid;
    int master, srv;

    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0) {
        ws.ws_row = 24;
        ws.ws_col = 80;
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;
    }

    master = pty_spawn_shell(&ws, &child_pid);
    if (master < 0) {
        perror("pty: failed to spawn shell");
        exit(1);
    }

    srv = socket_create();
    if (srv < 0) {
        perror("socket: failed to create");
        kill(child_pid, SIGTERM);
        waitpid(child_pid, NULL, 0);
        close(master);
        exit(1);
    }

    if (signal_setup() < 0) {
        perror("signal: failed to setup");
        kill(child_pid, SIGTERM);
        waitpid(child_pid, NULL, 0);
        close(master);
        socket_cleanup();
        exit(1);
    }

    while (1) {
        int client;

        if (tt_sigchld || tt_sigterm)
            break;

        client = socket_accept(srv);
        if (client < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        relay_loop(master, client, &ws);
        close(client);
    }

    close(master);
    socket_cleanup();

    if (child_pid > 0) {
        kill(child_pid, SIGTERM);
        waitpid(child_pid, NULL, 0);
    }
}
