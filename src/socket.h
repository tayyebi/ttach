#ifndef TTACH_SOCKET_H
#define TTACH_SOCKET_H

#include <sys/types.h>

int socket_create(void);
int socket_accept(int fd);
int socket_connect(void);
void socket_cleanup(void);

#endif
