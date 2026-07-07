#ifndef TTACH_STATE_H
#define TTACH_STATE_H

#include <sys/types.h>

void state_save(pid_t shell_pid);
void state_restore(int pty_fd);
void state_clear(void);

#endif
