#include <sys/socket.h> /* sockaddr_in */
#include <netdb.h> /* gethints() */
#include <sys/types.h>
#include <syslog.h>
#include <pthread.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "typedefs.h"
#include "socket_connections.h"
#include "gnssposget-server.h"

/* ---------------------------------------------  */
/* Private macro declarations */
/* ---------------------------------------------  */


#define NUM_THREADS                 (128)


/* ---------------------------------------------  */
/* Private types declarations */
/* ---------------------------------------------  */


typedef struct
{
    int conf_fd;
    struct sockaddr_in *client_addr;
    char* client_ip;
} task_params;


/* ---------------------------------------------  */
/* Private variables declarations */
/* ---------------------------------------------  */


pthread_t threads[NUM_THREADS];
task_params t_params[NUM_THREADS];
pthread_mutex_t file_mutex;


/* ---------------------------------------------  */
/* Private functions declarations */
/* ---------------------------------------------  */


Boolean gnssposget_server_task(void*);

/* ---------------------------------------------  */
/* Public functions */
/* ---------------------------------------------  */
void gnssposget_server_mainloop(int *listen_fd)
{
    int i = 1;
    int conf_fd;
    struct sockaddr_in client_addr;
    
    while(1)
    {
        /* Accept incoming connection */
        conf_fd = socket_connections_accept_incoming(&client_addr, listen_fd);
        t_params[i].client_addr = &client_addr;
        t_params[i].conf_fd = conf_fd;
        pthread_create(&threads[i], NULL, (void*)gnssposget_server_task, (void*)&t_params[i]);
        i++;

        if (i >= NUM_THREADS)
        {
            /* Reached max thread limit. Bye-bye */
            break;
        }
    }
}


void gnssposget_server_teardown_tasks(void)
{
    for(int i = 0; i < NUM_THREADS; i++)
    {
        close(t_params[i].conf_fd);
        //printClientIpAddress(FALSE, &t_params[i]);
        pthread_join(threads[i], NULL);
    }
    
}


/* ---------------------------------------------  */
/* Private functions */
/* ---------------------------------------------  */


Boolean gnssposget_server_task(void* arg)
{
    Boolean result = TRUE;
    task_params* arguments = (task_params*)arg;
    long offset = 0;
    U8* in_buf = NULL;
    U8 out_buf[] = "Hello, I am Mr.Biba\n";

    result &= socket_connections_read_data_from_client(arguments->conf_fd, &offset, &in_buf);
    syslog(LOG_INFO, "RECEIVED STRING FROM CLIENT: %s\n", in_buf);

    result &= socket_connections_send_data_to_client(arguments->conf_fd, &offset, &out_buf[0U]);

    //printClientIpAddress(TRUE, arguments);
    //result &= readClientDataToFile(arguments->conf_fd, &offset);
    //result &= sendDataBackToClient(arguments->conf_fd, &offset);

    /* Data block complete, close current connection */
    close(arguments->conf_fd);
    //printClientIpAddress(FALSE, arguments);

    return result;
}

