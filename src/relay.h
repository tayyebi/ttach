#ifndef TTACH_RELAY_H
#define TTACH_RELAY_H

#include <sys/types.h>
#include <sys/ioctl.h>

int relay_loop(int pty_fd, int client_fd, struct winsize *ws);

#endif
