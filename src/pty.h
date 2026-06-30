#ifndef TTACH_PTY_H
#define TTACH_PTY_H

#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>

int pty_spawn_shell(struct winsize *ws, pid_t *child_pid);
void pty_update_size(int master, struct winsize *ws);

#endif
