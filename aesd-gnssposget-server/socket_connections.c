#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <netdb.h> /* gethints() */
#include <errno.h>
#include <arpa/inet.h> /* get IP */
#include <sys/time.h> /* struct timeval */

#include <stdio.h>  
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "typedefs.h"
#include "gnssposget-server.h"
#include "aesdlog.h"
#include "socket_connections.h"

/* ---------------------------------------------  */
/* Private macros */
/* ---------------------------------------------  */


#define _XOPEN_SOURCE               (700)
#define SOCKET_DOMAIN               (PF_INET)
#define SOCKET_TYPE                 (SOCK_STREAM)
#define SOCKET_PORT                 ("9000")
#define SOCKET_INC_CONNECT_MAX      (50U)

#define DATA_BLOCK_SIZE             (512U)

/* ---------------------------------------------  */
/* Private variables declaration */
/* ---------------------------------------------  */


struct addrinfo *servinfo;


/* ---------------------------------------------  */
/* Private functions declaration */
/* ---------------------------------------------  */


static Boolean allocate_memory(char **buffer, U16 datablock_size);


/* ---------------------------------------------  */
/* Public functions */
/* ---------------------------------------------  */

void socket_connections_setup(int *listen_fd, Boolean is_daemon)
{
    freeaddrinfo(servinfo);

    struct addrinfo hints;
    pid_t pid;

    /* Open socket */
    if ((*listen_fd = socket(SOCKET_DOMAIN, SOCKET_TYPE, 0)) == FAIL)
    {
        aesdlog_err("Open socket error: %s", strerror(errno));
        exit(-1);
    }

    /* Setup to allow reusing socket and port*/
    if (setsockopt(*listen_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == FAIL)
    {
        aesdlog_err("setsockopt ADDR: %s", strerror(errno));
        exit(-1);
    }

    if (setsockopt(*listen_fd, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int)) == FAIL)
    {
        aesdlog_err("setsockopt PORT: %s", strerror(errno));
        exit(-1);
    }

    /* Setup addrinfo struct */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCKET_TYPE;
    if (getaddrinfo(NULL, SOCKET_PORT, &hints, &servinfo) != PASS)
    {
        aesdlog_err("getaddrinfo: %s", strerror(errno));
        exit(-1);
    }

    /* Bind addrinfo struct to socket listen_fd */
    if (bind(*listen_fd, servinfo->ai_addr, servinfo->ai_addrlen) == FAIL)
    {
        aesdlog_err("bind: %s", strerror(errno));
        exit(-1);
    }

    if (is_daemon == TRUE)
    {
        if ((pid = fork()) == FAIL)
        {
            aesdlog_err("fork: %s", strerror(errno));
            exit(-1);
        }

        if (pid > 0)
        {
            exit(0);
        }

        if (setsid() < 0)
        {
            exit(-1);
        }
    }

    /* Listen for incoming connections */
    if (listen(*listen_fd, SOCKET_INC_CONNECT_MAX) == FAIL)
    {
        aesdlog_err("listen: %s", strerror(errno));
        exit(-1);
    }
}


void socket_connections_teardown(void)
{
    freeaddrinfo(servinfo);
}


int socket_connections_accept_incoming(struct sockaddr_in* client_addr, int *listen_fd)
{
    int configured_fd;
    socklen_t client_addr_size;

    client_addr_size = sizeof(*client_addr);
    memset(client_addr, 0, client_addr_size);
    if ((configured_fd = accept(*listen_fd, (struct sockaddr*)client_addr, &client_addr_size)) == FAIL)
    {
        aesdlog_err("accept: %s", strerror(errno));
        exit(-1);
    }

    return configured_fd;
}

/*
*   This function reads data from a client socket and stores it in a buffer.
*
*   @param int configured_fd - The file descriptor of the client socket.
*   @param int timeout_sec - The timeout duration in seconds.
*   @param char** buf - Pointer to the buffer where the received data will be stored.
*
*   Returns:
*   - TRUE if data was successfully read,
*   - TRUE if timeout occured. Buffer is filled with "TIMEOUT"
*   - FALSE otherwise.
*/
Boolean socket_connections_read_data_from_client(int configured_fd, U8 timeout_sec, char** buf)
{
    Boolean result = TRUE;
    Boolean data_block_end = FALSE;
    int internal_cntr = 0;
    int retval;
    struct timeval timeout = {
        .tv_sec = (time_t)timeout_sec,
        .tv_usec = 0
    };

    /* Check if data comes for specified timeout */
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(configured_fd, &read_fds);
    retval = select((configured_fd + 1), &read_fds, NULL, NULL, &timeout);
    if (retval == -1)
    {
        aesdlog_err("select: %s", strerror(errno));
        result = FALSE;
    }
    else if (retval == 0)
    {
        /* Set TIMEOUT to buffer */
        char temp[] = "TIMEOUT";
        allocate_memory(buf, 8U);
        strcpy((char*)(*buf), (char*)&temp);
        result = TRUE;
    }
    else
    {
        /* Data available. Let's read it */
        while(data_block_end == FALSE)
        {
            allocate_memory(buf, DATA_BLOCK_SIZE);

            /* Read buffer from socket */
            for (internal_cntr = 0; internal_cntr < DATA_BLOCK_SIZE; internal_cntr++)
            {
                if (recv(configured_fd, (*buf + internal_cntr), sizeof(char), 0) == FAIL)
                {
                    aesdlog_err("recv: %s", strerror(errno));
                    aesdlog_err("configured_fd: %d", configured_fd);
                    data_block_end = TRUE;
                    result = FALSE;
                    break;
                }

                if (strcmp((char*)(*buf + internal_cntr), "\n") == PASS)
                {
                    aesdlog_dbg_info("readClientDataToFile(): Finished reading client data");
                    internal_cntr++; 
                    data_block_end = TRUE;
                    break;
                }
            }
        }
    }

    

    return result;
}

Boolean socket_connections_send_data_to_client(int configured_fd, char* buf)
{
    Boolean result = TRUE;
    Boolean data_block_end = FALSE;
    int internal_cntr;

    while (data_block_end == FALSE)
    {
        internal_cntr = strlen((char*)buf);

        if (send(configured_fd, buf, internal_cntr, 0) == FAIL)
        {
            aesdlog_err("send: %s", strerror(errno));
            data_block_end = TRUE;
            result = FALSE;
        }
        aesdlog_dbg_info("data sent to client: %s", buf);

        data_block_end = TRUE;
    }

    return result;
}


/* ---------------------------------------------  */
/* Private functions */
/* ---------------------------------------------  */
static Boolean allocate_memory(char **buffer, U16 datablock_size)
{
    Boolean result = TRUE;
    *buffer = (char*)calloc(datablock_size, sizeof(char));
    if (*buffer == NULL)
    {
        printf("allocate_memory(): calloc returned NULL!\n");
        result = FALSE;
    }

    return result;
}
