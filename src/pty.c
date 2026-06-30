#include "pty.h"

#include <pty.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>

int pty_spawn_shell(struct winsize *ws, pid_t *child_pid) {
    int master;
    pid_t pid;

    pid = forkpty(&master, NULL, NULL, ws);
    if (pid < 0)
        return -1;

    if (pid == 0) {
        const char *shell;
        struct passwd *pw;

        pw = getpwuid(getuid());
        shell = (pw && pw->pw_shell && pw->pw_shell[0])
                    ? pw->pw_shell : "/bin/sh";

        setenv("SHELL", shell, 1);
        setenv("TERM", "xterm-256color", 1);

        execlp(shell, shell, "-l", (char *)NULL);
        execlp(shell, shell, (char *)NULL);
        _exit(1);
    }

    *child_pid = pid;
    return master;
}

void pty_update_size(int master, struct winsize *ws) {
    ioctl(master, TIOCSWINSZ, ws);
}
