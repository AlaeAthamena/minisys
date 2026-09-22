#ifndef NET_H
#define NET_H

#include "minisys.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

typedef struct {
    int fd;
    struct sockaddr_in address;
    socklen_t addr_len;
} client_conn_t;

int create_server_socket(int port);
int set_nonblocking(int fd);
void close_connection(client_conn_t *conn);

#endif // NET_H
