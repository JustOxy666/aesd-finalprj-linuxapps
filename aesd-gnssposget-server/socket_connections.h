#ifndef SOCKET_CONNECTIONS_H
#define SOCKET_CONNECTIONS_H

#include "typedefs.h"

extern void socket_connections_setup(int *listen_fd, Boolean is_daemon);
extern void socket_connections_teardown(void);
extern int socket_connections_accept_incoming(struct sockaddr_in* client_addr, int* listen_fd);
Boolean socket_connections_read_data_from_client(int conf_fd, U8 timeout_sec, U8** buf);
Boolean socket_connections_send_data_to_client(int configured_fd, U8* buf);


#endif /* SOCKET_CONNECTIONS_H */
