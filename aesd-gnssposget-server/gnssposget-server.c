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
#define CLIENT_RECEIVE_TIMEOUT      (2U)


/* ---------------------------------------------  */
/* Private types declarations */
/* ---------------------------------------------  */


typedef struct
{
    int conf_fd;
    struct sockaddr_in *client_addr;
    char* client_ip;
} task_params;

typedef enum 
{
    STATE_INIT = 0,
    STATE_WAITING_FOR_CLIENT,
    STATE_START_REQUESTED,
    STATE_WORKING,
    STATE_ABORT_REQUESTED,
    STATE_FINISHED,
    STATE_DONE,
    STATE_ERROR,
    STATE_UNEXPECTED_ERROR,
    STATE_NUM_STATES = 8
} serverapp_states; 

struct state_machine_params
{
    U8* in_buf;
    int conf_fd;
    Boolean run_listener;
    serverapp_states current_state;
};


/* ---------------------------------------------  */
/* Private variables declarations */
/* ---------------------------------------------  */


pthread_t threads[NUM_THREADS];
pthread_t listener_thread;
task_params t_params[NUM_THREADS];
pthread_mutex_t file_mutex;

Boolean teardown_requested = FALSE;
Boolean status_requested = FALSE;


/* ---------------------------------------------  */
/* Private functions declarations */
/* ---------------------------------------------  */

void listener_task(void*);
Boolean gnssposget_server_task(void*);
void server_run(task_params* arguments);
void read_client_error(serverapp_states* current_state);
Boolean send_to_client(int configured_fd, struct state_machine_params* sm_params, U8* buf);
void teardown(void);

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

void gnssposget_server_request_teardown(void)
{
    teardown_requested = TRUE;
}


/* ---------------------------------------------  */
/* Private functions */
/* ---------------------------------------------  */


Boolean gnssposget_server_task(void* arg)
{
    Boolean result = TRUE;
    task_params* arguments = (task_params*)arg;

    server_run(arguments);

    // U8* in_buf = NULL;
    // U8 out_buf[] = "Hello, I am Mr.Biba\n";

    // result &= socket_connections_read_data_from_client(arguments->conf_fd, &offset, &in_buf);
    // syslog(LOG_INFO, "RECEIVED STRING FROM CLIENT: %s\n", in_buf);

    // result &= socket_connections_send_data_to_client(arguments->conf_fd, &offset, &out_buf[0U]);

    //printClientIpAddress(TRUE, arguments);
    //result &= readClientDataToFile(arguments->conf_fd, &offset);
    //result &= sendDataBackToClient(arguments->conf_fd, &offset);
    
    teardown();

    /* Data block complete, close current connection */
    close(arguments->conf_fd);
    //printClientIpAddress(FALSE, arguments);

    return result;
}

void server_run(task_params* arguments)
{
    Boolean is_running = TRUE;
    struct state_machine_params sm_params = {
        .in_buf = NULL,
        .conf_fd = arguments->conf_fd,
        .run_listener = TRUE,
        .current_state = STATE_INIT
    };

    while(is_running == TRUE)
    {
        if (teardown_requested == TRUE)
        {
            sm_params.run_listener = FALSE;
            is_running = FALSE;
            break;
        }

        switch (sm_params.current_state)
        {
            case STATE_INIT:
            {
                /* Start receiving messages */
                printf("STATE_INIT\n");
                printf("in_buf addr=0x%X\n", (unsigned int)sm_params.in_buf);
                sm_params.run_listener = TRUE;
                pthread_create(&listener_thread, NULL, (void*)listener_task, (void*)&sm_params);
                sm_params.current_state = STATE_WAITING_FOR_CLIENT;
            }
                break;
            case STATE_WAITING_FOR_CLIENT:
            {
                /* Listener is running. Just wait for a next state */
                break;
            }
            case STATE_START_REQUESTED:
            {
                /* TODO: Poll GNSS status */

                /* TODO: GNSS status received and it's good */
                (void)send_to_client(arguments->conf_fd, &sm_params, "STATE_START_REQUESTED^WORKING^Signal=100percent\n");
                sm_params.current_state = STATE_WORKING;
            }
            break;
            case STATE_WORKING:
            {
                /* TODO: Poll data from GNSS */

                (void)send_to_client(arguments->conf_fd, &sm_params, "STATE_WORKING^RUNNING_STATUS^0#3.53\n");
                (void)send_to_client(arguments->conf_fd, &sm_params, "STATE_WORKING^RUNNING_STATUS^1#6.79\n");
                (void)send_to_client(arguments->conf_fd, &sm_params, "STATE_WORKING^RUNNING_TIMEOUT^2#30\n");
                sm_params.current_state = STATE_DONE;
            }
            break;
            case STATE_ABORT_REQUESTED:
                // Handle abort requested
                break;
            case STATE_FINISHED:
                // Handle finished
                printf("STATE_FINISHED\n");
                sm_params.current_state = STATE_INIT;
                break;
            case STATE_DONE:
                // Handle done
                syslog(LOG_INFO, "State done");
                sm_params.run_listener = FALSE;
                pthread_join(listener_thread, NULL);
                sm_params.current_state = STATE_FINISHED;
                break;
            case STATE_ERROR:
                // Handle error
                teardown();
                is_running = FALSE;
                break;
            case STATE_UNEXPECTED_ERROR:
                // Handle unexpected error
                teardown();
                is_running = FALSE;
                break;
            default:
                // Handle unknown state
                teardown();
                is_running = FALSE;
                break;
        }
    }
}

void listener_task(void* arg)
{
    printf("Started listener thread\n");
    struct state_machine_params* params = (struct state_machine_params*)arg;
    while ((params->run_listener) == TRUE)
    {
        if (socket_connections_read_data_from_client(params->conf_fd, 
                                                     CLIENT_RECEIVE_TIMEOUT,
                                                     &params->in_buf) == TRUE)
        {
            if (strcmp(params->in_buf, "TIMEOUT") == 0)
            {
                free(params->in_buf);
                params->in_buf = NULL;
                continue;
            }

            if (strcmp(params->in_buf, "REQUEST_ABORT\n") == 0)
            {
                free(params->in_buf);
                params->in_buf = NULL;
                if ((params->current_state == STATE_START_REQUESTED) ||
                    (params->current_state == STATE_WORKING))
                    {
                        syslog(LOG_INFO, "Received REQUEST_ABORT message");
                        /* TODO: Implement abort logic */
                        params->current_state = STATE_DONE;
                        params->run_listener = FALSE;
                    }
            }
            else if (strcmp(params->in_buf, "REQUEST_STATUS\n") == 0)
            {
                free(params->in_buf);
                params->in_buf = NULL;
                if (params->current_state == STATE_WORKING)
                {
                    syslog(LOG_INFO, "Received REQUEST_STATUS message");
                    /* TODO: Implement getting status logic */
                    (void)send_to_client(params->conf_fd, params, "STATE_WORKING^Signal=69percent\n");
                }
            }
            else if (strcmp(params->in_buf, "STATE_INIT\n") == 0)
            {
                free(params->in_buf);
                params->in_buf = NULL;
                syslog(LOG_INFO, "Received STATE_INIT message");
                /* TODO: Start getting GNSS here */

                params->current_state = STATE_START_REQUESTED;
            }
            else
            {
                free(params->in_buf);
                syslog(LOG_INFO, "Received generic message: %s", params->in_buf);
            }
        }
        else
        {
            read_client_error(&params->current_state);
            params->run_listener = FALSE;
        }
    }
}

void read_client_error(serverapp_states* current_state)
{
    printf("Failed to read data from client\n");
    syslog(LOG_PERROR, "Failed to read data from client");
    *current_state = STATE_UNEXPECTED_ERROR;
}

Boolean send_to_client(int configured_fd, struct state_machine_params* sm_params, U8* buf)
{
    if (socket_connections_send_data_to_client(configured_fd, &buf[0U]) == FALSE)
    {
        /* Something went terribly wrong */
        syslog(LOG_PERROR, "ERROR sending message to client");
        /* TODO: Kill everything GNSS related */

        sm_params->current_state = STATE_UNEXPECTED_ERROR;
        sm_params->run_listener = FALSE;
        return FALSE;
    }
}

void teardown(void)
{
    pthread_join(listener_thread, NULL);
    for(int i = 0; i < NUM_THREADS; i++)
    {
        close(t_params[i].conf_fd);
        //printClientIpAddress(FALSE, &t_params[i]);
        pthread_join(threads[i], NULL);
    }
}

