#include "state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

#define STATE_DIR  ".local/share/ttach"
#define STATE_FILE "state"

static char   state_path[1024];
static int    path_ready;

static void ensure_path(void) {
    char dir[512];
    const char *home;
    if (path_ready) return;
    home = getenv("HOME");
    if (!home || !home[0]) home = "/tmp";
    snprintf(dir, sizeof(dir), "%s/%s", home, STATE_DIR);
    mkdir(dir, 0755);
    snprintf(state_path, sizeof(state_path), "%s/%s", dir, STATE_FILE);
    path_ready = 1;
}

static int should_skip_var(const char *entry, int name_len) {
    static const char *list[] = {
        "HOME", "USER", "LOGNAME", "SHELL", "TERM",
        "PATH", "PWD", "OLDPWD", "LANG", "HOSTNAME",
        "SHLVL", "MAIL", "DISPLAY", "COLORTERM",
        "WINDOWID", "TTACH_SESSION", "_",
        NULL
    };
    static const char *pfx[] = {
        "LC_", "XDG_", "DBUS_", "SSH_", "WAYLAND_",
        "HIST", "TERM_", "GNOME_", "GTK_", "QT_",
        "DESKTOP_", "SESSION_", "BASH_FUNC_",
        "LS_COLORS", "LESS",
        NULL
    };
    int i;
    for (i = 0; list[i]; i++)
        if (name_len == (int)strlen(list[i]) &&
            strncmp(entry, list[i], name_len) == 0)
            return 1;
    for (i = 0; pfx[i]; i++)
        if (strncmp(entry, pfx[i], strlen(pfx[i])) == 0)
            return 1;
    return 0;
}

void state_save(pid_t shell_pid) {
    char cwd[4096], proc[256], buf[65536];
    int len, fd;
    FILE *f;

    ensure_path();

    snprintf(proc, sizeof(proc), "/proc/%d/cwd", shell_pid);
    len = (int)readlink(proc, cwd, sizeof(cwd) - 1);
    if (len <= 0) return;
    cwd[len] = '\0';

    f = fopen(state_path, "w");
    if (!f) return;

    fprintf(f, "cwd=%s\n", cwd);
    fprintf(f, "time=%ld\n", (long)time(NULL));

    snprintf(proc, sizeof(proc), "/proc/%d/environ", shell_pid);
    fd = open(proc, O_RDONLY);
    if (fd >= 0) {
        int n = (int)read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            char *p = buf;
            buf[n] = '\0';
            while (p < buf + n) {
                char *eq = strchr(p, '=');
                int nl = eq ? (int)(eq - p) : (int)strlen(p);
                if (nl > 0 && !should_skip_var(p, nl))
                    fprintf(f, "ev_%s\n", p);
                p += strlen(p) + 1;
            }
        }
    }

    fclose(f);
}

static void write_str(int fd, const char *s) {
    write(fd, s, strlen(s));
}

static void write_export(int fd, const char *entry) {
    const char *eq = strchr(entry, '=');
    char name[256];

    if (!eq) return;
    int nl = (int)(eq - entry);
    if (nl >= (int)sizeof(name)) return;
    memcpy(name, entry, nl);
    name[nl] = '\0';

    const char *val = eq + 1;
    int has_special = 0;
    for (const char *s = val; *s; s++) {
        if (*s == '\'' || *s == '\\' || *s == '\n' ||
            *s == '\r' || *s == '\t' || *s == '"' ||
            *s == '$' || *s == '`')
            has_special = 1;
    }

    write_str(fd, "export ");
    write_str(fd, name);
    write_str(fd, "='");
    for (const char *s = val; *s; s++) {
        if (*s == '\'')
            write_str(fd, "'\\''");
        else
            write(fd, s, 1);
    }
    write_str(fd, "'\n");
    (void)has_special;
}

void state_restore(int pty_fd) {
    char line[65536], cwd[4096], time_buf[64];
    FILE *f;
    int restored = 0;

    ensure_path();
    f = fopen(state_path, "r");
    if (!f) return;

    cwd[0] = '\0';
    time_buf[0] = '\0';

    while (fgets(line, sizeof(line), f)) {
        int len = (int)strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';

        if (strncmp(line, "cwd=", 4) == 0) {
            snprintf(cwd, sizeof(cwd), "%s", line + 4);
            restored = 1;
        } else if (strncmp(line, "time=", 5) == 0) {
            snprintf(time_buf, sizeof(time_buf), "%s", line + 5);
        } else if (strncmp(line, "ev_", 3) == 0) {
            write_export(pty_fd, line + 3);
        }
    }
    fclose(f);

    if (restored && cwd[0]) {
        char cmd[4352];
        snprintf(cmd, sizeof(cmd), "cd '%s' 2>/dev/null\n", cwd);
        write_str(pty_fd, cmd);
    }
    if (restored && time_buf[0]) {
        time_t ts = (time_t)atol(time_buf);
        struct tm *tm = localtime(&ts);
        char tstr[64], cmd[256];
        if (tm) {
            strftime(tstr, sizeof(tstr), "%Y-%m-%d %H:%M", tm);
            snprintf(cmd, sizeof(cmd),
                     "echo '[ttach: restored session from %s]'\n",
                     tstr);
            write_str(pty_fd, cmd);
        }
    }
}

void state_clear(void) {
    ensure_path();
    unlink(state_path);
}
