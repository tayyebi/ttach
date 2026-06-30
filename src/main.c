#include "server.h"
#include "client.h"
#include "socket.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int fd = socket_connect();
    if (fd >= 0) {
        client_run(fd);
        return 0;
    }

    server_run();
    return 0;
}
