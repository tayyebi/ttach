#ifndef TTACH_SIGNAL_H
#define TTACH_SIGNAL_H

#include <signal.h>

extern volatile sig_atomic_t tt_sigchld;
extern volatile sig_atomic_t tt_sigterm;
extern volatile sig_atomic_t tt_sigwinch;

int signal_setup(void);

#endif
