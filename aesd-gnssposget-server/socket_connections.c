#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h> /* gethints() */
#include <syslog.h>
#include <errno.h>
#include <arpa/inet.h> /* get IP */

#include <stdio.h>  
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "typedefs.h"
#include "gnssposget-server.h"
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


static Boolean allocate_memory(U8 **buffer, U16 datablock_size);


/* ---------------------------------------------  */
/* Public functions */
/* ---------------------------------------------  */

void socket_connections_setup(int *listen_fd, Boolean is_daemon)
{
    freeaddrinfo(servinfo);

    struct addrinfo hints;
    pid_t pid;

    //client_started_sending = FALSE;

    /* Init mutex */
    //pthread_mutex_init(&file_mutex, NULL);

    /* Open socket */
    if ((*listen_fd = socket(SOCKET_DOMAIN, SOCKET_TYPE, 0)) == FAIL)
    {
        printf("Open socket error: %s\n", strerror(errno));
        exit(-1);
    }

    /* Setup to allow reusing socket and port*/
    if (setsockopt(*listen_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == FAIL)
    {
        printf("setsockopt ADDR: %s\n", strerror(errno));
        exit(-1);
    }

    if (setsockopt(*listen_fd, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int)) == FAIL)
    {
        printf("setsockopt PORT: %s\n", strerror(errno));
        exit(-1);
    }

    /* Setup addrinfo struct */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCKET_TYPE;
    if (getaddrinfo(NULL, SOCKET_PORT, &hints, &servinfo) != PASS)
    {
        printf("getaddrinfo: %s\n", strerror(errno));
        exit(-1);
    }

    /* Bind addrinfo struct to socket listen_fd */
    if (bind(*listen_fd, servinfo->ai_addr, servinfo->ai_addrlen) == FAIL)
    {
        printf("bind: %s\n", strerror(errno));
        exit(-1);
    }

    if (is_daemon == TRUE)
    {
        if ((pid = fork()) == FAIL)
        {
            printf("fork: %s\n", strerror(errno));
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
        printf("listen: %s\n", strerror(errno));
        exit(-1);
    }
}


void socket_connections_teardown(void)
{
    //timestamp_thread_exit = TRUE;
    gnssposget_server_teardown_tasks();
    freeaddrinfo(servinfo);
    //pthread_mutex_destroy(&file_mutex);
}


int socket_connections_accept_incoming(struct sockaddr_in* client_addr, int *listen_fd)
{
    int configured_fd;
    socklen_t client_addr_size;

    client_addr_size = sizeof(*client_addr);
    memset(client_addr, 0, client_addr_size);
    if ((configured_fd = accept(*listen_fd, (struct sockaddr*)client_addr, &client_addr_size)) == FAIL)
    {
        printf("accept: %s\n", strerror(errno));
        exit(-1);
    }

    return configured_fd;
}


Boolean socket_connections_read_data_from_client(int configured_fd, long *offset, U8** buf)
{
    Boolean result = TRUE;
    FILE* fstream;
    Boolean data_block_end = FALSE;
    int internal_cntr = 0;

    while(data_block_end == FALSE)
    {
        allocate_memory(buf, DATA_BLOCK_SIZE);

        /* Read buffer from socket */
        for (internal_cntr = 0; internal_cntr < DATA_BLOCK_SIZE; internal_cntr++)
        {
            if (recv(configured_fd, (*buf + internal_cntr), sizeof(U8), 0) == FAIL)
            {
                printf("recv_read: %s\n", strerror(errno));
                printf("configured_fd: %d\n", configured_fd);
                data_block_end = TRUE;
                result = FALSE;
            }

//             if (client_started_sending == FALSE)
//             {
// #ifdef DEBUG_ON
//                 printf("readClientDataToFile(): Started timestamping now\n");
// #endif /* DEBUG_ON */
//                 client_started_sending = TRUE;
//             }

            if (strcmp((U8*)(*buf + internal_cntr), "\n") == PASS)
            {
                printf("readClientDataToFile(): Finished reading client data\n");
                /* \n is two symbols */
                internal_cntr++; 
                data_block_end = TRUE;
                break;
            }
        }
    }

    return result;
}


Boolean socket_connections_send_data_to_client(int configured_fd, long *offset, U8* buf)
{
    Boolean result = TRUE;
    FILE* fstream;
    Boolean data_block_end = FALSE;
    int internal_cntr;
    long counter = 0; /* total bytes counter */

    while (data_block_end == FALSE)
    {
        // /* Send data back to client */
        // /* Data is passed by app */
        // //allocateMemory(&buf, DATA_BLOCK_SIZE);

        // /* ------------- ENTER CRITICAL SECTION -------------- */
        // pthread_mutex_lock(&file_mutex);
        // if ((fstream = fopen((char*)SOCKET_DATA_FILEPATH, "r+")) == NULL)
        // {
        //     printf("fstream: %s\n", strerror(errno));
        //     data_block_end = TRUE;
        //     result = FALSE;
        // }

        // /* Put file pointer before next block */
        // off_t cur_offset = lseek(fileno(fstream), (counter + *offset), SEEK_SET);
        // for (internal_cntr = 0; internal_cntr < DATA_BLOCK_SIZE; internal_cntr++)
        // {
        //     counter++;
        //     if (fread(&buf[internal_cntr], sizeof(char), sizeof(char), fstream) == 0)
        //     {
        //         data_block_end = TRUE;
        //         break;
        //     }
        // }

        internal_cntr = strlen((U8*)buf);

        if (send(configured_fd, buf, internal_cntr, 0) == FAIL)
        {
            printf("recv_send: %s\n", strerror(errno));
            data_block_end = TRUE;
            result = FALSE;
        }

        data_block_end = TRUE;

        // free(buf);
        // fflush(fstream);
        // fclose(fstream);
        // pthread_mutex_unlock(&file_mutex);
        /* ------------- EXIT CRITICAL SECTION -------------- */
    }

    return result;
}



/* ---------------------------------------------  */
/* Private functions */
/* ---------------------------------------------  */
static Boolean allocate_memory(U8 **buffer, U16 datablock_size)
{
    Boolean result = TRUE;
    *buffer = (U8*)calloc(datablock_size, sizeof(U8));
    if (*buffer == NULL)
    {
        printf("allocate_memory(): calloc returned NULL!\n");
        result = FALSE;
    }

    return result;
}

