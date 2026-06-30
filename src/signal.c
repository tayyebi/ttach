#include "signal.h"
#include <stddef.h>

volatile sig_atomic_t tt_sigchld;
volatile sig_atomic_t tt_sigterm;
volatile sig_atomic_t tt_sigwinch;

static void handle_sigchld(int sig) {
    (void)sig;
    tt_sigchld = 1;
}

static void handle_sigterm(int sig) {
    (void)sig;
    tt_sigterm = 1;
}

static void handle_sigwinch(int sig) {
    (void)sig;
    tt_sigwinch = 1;
}

int signal_setup(void) {
    struct sigaction sa;

    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sa.sa_handler = handle_sigchld;
    if (sigaction(SIGCHLD, &sa, NULL) < 0) return -1;

    sa.sa_handler = handle_sigterm;
    if (sigaction(SIGTERM, &sa, NULL) < 0) return -1;

    sa.sa_handler = handle_sigwinch;
    if (sigaction(SIGWINCH, &sa, NULL) < 0) return -1;

    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGINT, &sa, NULL) < 0) return -1;

    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGHUP, &sa, NULL) < 0) return -1;

    return 0;
}
