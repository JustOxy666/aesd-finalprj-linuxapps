#ifndef SOCKET_CONNECTIONS_H
#define SOCKET_CONNECTIONS_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "typedefs.h"

extern void socket_connections_setup(int *listen_fd, Boolean is_daemon);
extern void socket_connections_teardown(void);
extern int socket_connections_accept_incoming(struct sockaddr_in* client_addr, int* listen_fd);
Boolean socket_connections_read_data_from_client(int conf_fd, U8 timeout_sec, char** buf);
Boolean socket_connections_send_data_to_client(int configured_fd, char* buf);


#endif /* SOCKET_CONNECTIONS_H */
