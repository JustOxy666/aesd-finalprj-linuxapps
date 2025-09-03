#include <sys/socket.h> /* sockaddr_in */
#include <netdb.h> /* gethints() */
#include <sys/types.h>
#include <pthread.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "typedefs.h"
#include "socket_connections.h"
#include "gnssdata.h"
#include "accelmeter-app.h"
#include "aesdtimer.h"
#include "aesdlog.h"

#include "gnssposget-server.h"

/* ---------------------------------------------  */
/* Private macro declarations */
/* ---------------------------------------------  */


#define NUM_THREADS                 (128)
#define CLIENT_RECEIVE_TIMEOUT      (2U)

#define SLEEP_WAIT_TIMEOUT_S        (3U)
#define POLL_STATUS_TIMEOUT_S       (15U)
#define ACCEL_TIMEOUT_S             (30U)


/* ---------------------------------------------  */
/* Private types declarations */
/* ---------------------------------------------  */


typedef struct
{
    int conf_fd;
    struct sockaddr_in *client_addr;
    char* client_ip;
    Boolean run_server;
} task_params;

typedef enum 
{
    STATE_INIT = 0,
    STATE_WAITING_FOR_CLIENT,
    STATE_START_REQUESTED,
    STATE_START_REQUESTED_POLL_SIGNAL,
    STATE_WORKING,
    STATE_WORKING_WAIT_ACCEL,
    STATE_WORKING_MEASURE,
    STATE_WORKING_ANALYZE,
    STATE_ABORT_REQUESTED,
    STATE_FINISHED,
    STATE_DONE,
    STATE_ERROR,
    STATE_UNEXPECTED_ERROR,
    STATE_NUM_STATES
} serverapp_states; 

struct state_machine_params
{
    char *in_buf;
    int conf_fd;
    Boolean run_listener;
    serverapp_states current_state;
};


/* ---------------------------------------------  */
/* static variables declarations */
/* ---------------------------------------------  */


pthread_t server_thread;
pthread_t listener_thread;
task_params t_params;
pthread_mutex_t state_mutex;

Boolean teardown_requested;
Boolean status_requested = FALSE;


/* ---------------------------------------------  */
/* static functions declarations */
/* ---------------------------------------------  */

static void listener_task(void*);
static Boolean gnssposget_server_task(void*);
static void server_run(task_params* arguments);
static void read_client_error(serverapp_states* current_state);
static Boolean send_to_client(struct state_machine_params* sm_params, char *buf);
static serverapp_states get_state(serverapp_states *state_var);
static void send_status_data_to_client(struct state_machine_params* sm_params, char *additional_info);
static void set_state(serverapp_states *state_var, serverapp_states new_state);
static void teardown(void);

/* ---------------------------------------------  */
/* Public functions */
/* ---------------------------------------------  */
void gnssposget_server_mainloop(int *listen_fd)
{
    int conf_fd;
    struct sockaddr_in client_addr;
    teardown_requested = FALSE;
    
    pthread_mutex_init(&state_mutex, NULL);

    /* Accept incoming connection */
    conf_fd = socket_connections_accept_incoming(&client_addr, listen_fd);
    aesdlog_info("Connected to client");
    t_params.client_addr = &client_addr;
    t_params.conf_fd = conf_fd;
    pthread_create(&server_thread, NULL, (void*)gnssposget_server_task, (void*)&t_params);

    while (teardown_requested == FALSE)
    {
        /* Run until teardown requested */
        sleep(1);
    }

    teardown();
    aesdlog_info("gnssposget - leaving main loop");
}

void gnssposget_server_request_teardown(void)
{
    teardown_requested = TRUE;
}


/* ---------------------------------------------  */
/* Private functions */
/* ---------------------------------------------  */


static Boolean gnssposget_server_task(void* arg)
{
    Boolean result = TRUE;
    task_params* arguments = (task_params*)arg;

    aesdlog_dbg_info("server_run");
    server_run(arguments);
    
    // Boolean status = FALSE;
    // double speed, timestamp;
    // char *status_string;
    // while (teardown_requested == FALSE)
    // {
    //     while(status == FALSE)
    //     {
    //         gnssdata_get_status_flag = TRUE;
    //         status = gnssdata_poll_status();
    //         gnssdata_get_status(&status_string);
    //         syslog(LOG_INFO, "%s", status_string);
    //         free(status_string);
    //         sleep(1);
    //     }

    //     gnssdata_get_status_flag = FALSE;
    //     syslog(LOG_INFO, "gnssposget_server_task(): Fix found");

    //     speed = gnssdata_get_speed();
    //     syslog(LOG_INFO, "gnssposget - SPEED: %lf", speed);
    //     timestamp = gnssdata_get_timestamp();
    //     syslog(LOG_INFO, "gnssposget - TIMESTAMP: %lf", timestamp);
    //     sleep(1);
    // }
    
    /* Close current connection */
    close(arguments->conf_fd);
    aesdlog_info("gnssposget - closing server_thread");

    return result;
}

static void server_run(task_params* arguments)
{
    Boolean is_running = TRUE;
    serverapp_states cur_state;
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

        cur_state = get_state(&sm_params.current_state);
        switch (cur_state)
        {
            case STATE_INIT:
            {
                /* Start receiving messages */
                aesdlog_dbg_info("STATE_INIT message received");
                sm_params.run_listener = TRUE;
                set_state(&sm_params.current_state, STATE_WAITING_FOR_CLIENT);
                pthread_create(&listener_thread, NULL, (void*)listener_task, (void*)&sm_params);
                break;
            }
            case STATE_WAITING_FOR_CLIENT:
            {
                /* Listener is running. Just wait for a next state */
                break;
            }
            case STATE_START_REQUESTED:
            {
                gnssdata_start();
                timer_start();
                set_state(&sm_params.current_state, STATE_START_REQUESTED_POLL_SIGNAL);
                break;
            }
            case STATE_START_REQUESTED_POLL_SIGNAL:
            {
                if (gnssdata_poll_status() == TRUE)
                {
                    /* We have a fix! */
                    aesdlog_dbg_info("STATE_START_REQUESTED_POLL_SIGNAL: Fix obtained");
                    char sendstr[100];
                    char *status_string;
                    gnssdata_get_status_flag = FALSE;
                    timer_stop();
                    gnssdata_get_status(&status_string);
                    sprintf(sendstr, "STATE_START_REQUESTED^WORKING^%s\n", status_string);
                    free(status_string);

                    /* Read timestamp and speed once to skip possible old data */
                    (void)gnssdata_get_speed();
                    (void)gnssdata_get_timestamp();

                    /* Ready to capture GNSS data */
                    set_state(&sm_params.current_state, STATE_WORKING);
                    (void)send_to_client(&sm_params, sendstr);
                }
                else
                {
                    /* No fix yet. Check timeout */
                    if (timer_is_elapsed(POLL_STATUS_TIMEOUT_S) == TRUE)
                    {
                        /* Timeout occurred. Send info to client */
                        aesdlog_err("STATE_START_REQUESTED_POLL_SIGNAL: No fix obtained");
                        char sendstr[100];
                        gnssdata_get_status_flag = FALSE;
                        timer_stop();
                        gnssdata_stop();
                        sprintf(sendstr, "STATE_START_REQUESTED^NO_SIGNAL^%d\n", (int)POLL_STATUS_TIMEOUT_S);
                        (void)send_to_client(&sm_params, sendstr);
                        set_state(&sm_params.current_state, STATE_DONE);
                    }
                    else
                    {
                        aesdlog_dbg_info("Waiting for fix...");
                        sleep(SLEEP_WAIT_TIMEOUT_S);
                    }
                }
            }
            break;
            case STATE_WORKING:
            {
                timer_start();
                accelmeter_app_start();
                set_state(&sm_params.current_state, STATE_WORKING_WAIT_ACCEL);
            }
            break;
            /* **************** */
            /* Wait for vehicle to start accelerating here */
            /* **************** */
            case STATE_WORKING_WAIT_ACCEL:
            {
                /* Get speed & timestamp data and validate it */
                double timestamp, speed;
                Boolean add_result = FALSE;
                if (((speed = gnssdata_get_speed()) != -1.0) &&
                    ((timestamp = gnssdata_get_timestamp()) != -1.0))
                {
                    /* Only do checks if received new timestamp */
                    add_result = accelmeter_app_add_data(timestamp, speed);
                    if ((speed >= (double)ACCELMETER_APP_START_SPEED_THRESHOLD) && (add_result == TRUE))
                    {
                        /* Acceleration started. Restart timer */
                        aesdlog_dbg_info("STATE_WORKING_WAIT_ACCEL: Acceleration started at timestamp %.2f", timestamp);
                        timer_stop();
                        timer_start();
                        set_state(&sm_params.current_state, STATE_WORKING_MEASURE);
                    }
                    else
                    {
                        /* Acceleration not started */
                        if (timer_is_elapsed(ACCEL_TIMEOUT_S) == TRUE)
                        {
                            /* Timeout occurred. Send info to client */
                            char sendstr[100];
                            aesdlog_err("STATE_WORKING_WAIT_ACCEL: No acceleration detected");
                            timer_stop();
                            accelmeter_app_stop();
                            gnssdata_stop();
                            sprintf(sendstr, "STATE_WORKING^RUNNING_TIMEOUT^0#%d\n", (int)ACCEL_TIMEOUT_S);
                            (void)send_to_client(&sm_params, sendstr);
                            set_state(&sm_params.current_state, STATE_DONE);
                        }
                        else
                        {
                            /* Still waiting. Check if we can free some memory */
                            if (accelmeter_app_get_data_size() > (int)ACCELMETER_APP_INITIAL_CAPACITY)
                            {
                                accelmeter_app_stop();
                                accelmeter_app_start();
                            }
                        }
                    }
                }
                else
                {
                    /* Data invalid! Bailing out if number of incorrect instances exceeded */
                    accelmeter_app_handle_incorrect_data();
                    if (accelmeter_app_get_incorrect_data_count() >= ACCELMETER_APP_MAX_INCORRECT_DATA_INSTANCES)
                    {
                        aesdlog_err("STATE_WORKING_WAIT_ACCEL: Invalid data received");
                        accelmeter_app_stop();
                        timer_stop();
                        gnssdata_stop();
                        send_status_data_to_client(&sm_params, "RUNNING_ERROR^");
                        set_state(&sm_params.current_state, STATE_DONE);
                    }
                }

                usleep(200);
            }
            break;
            /* **************** */
            /* Accelerating now. Get timestamps and speed, detect checkpoints and timeout */
            /* **************** */
            case STATE_WORKING_MEASURE:
            {
                /* Get speed & timestamp data and validate it */
                double timestamp, speed, checkpoint;
                Boolean add_result = FALSE;
                if (((speed = gnssdata_get_speed()) != -1.0) &&
                    ((timestamp = gnssdata_get_timestamp()) != -1.0))
                {
                    /* Only do checks if received new timestamp */
                    add_result = accelmeter_app_add_data(timestamp, speed);
                    checkpoint = accelmeter_app_get_current_checkpoint();
                    if ((checkpoint == 0.0) && (add_result == TRUE))
                    {
                        /* Reached final checkpoint */
                        aesdlog_dbg_info("STATE_WORKING_MEASURE: Final checkpoint reached");
                        timer_stop();
                        gnssdata_stop();
                        set_state(&sm_params.current_state, STATE_WORKING_ANALYZE);
                    }
                    else if ((speed >= (double)checkpoint) && (add_result == TRUE))
                    {
                        /* Passed checkpoint */
                        aesdlog_dbg_info("STATE_WORKING_MEASURE: Passed checkpoint %d at timestamp %.3lf", (int)checkpoint, timestamp);
                        accelmeter_app_set_checkpoint(checkpoint);
                        timer_stop();
                        timer_start();
                    }
                    else
                    {
                        /* No checkpoint reached. Check timeout */
                        if (timer_is_elapsed(ACCEL_TIMEOUT_S) == TRUE)
                        {
                            timer_stop();
                            gnssdata_stop();
                            set_state(&sm_params.current_state, STATE_WORKING_ANALYZE);
                        }
                        else
                        {
                            /* Still waiting. Do nothing */
                        }
                    }
                }
                else
                {
                    /* Data invalid! Bailing out if number of incorrect instances exceeded */
                    accelmeter_app_handle_incorrect_data();
                    if (accelmeter_app_get_incorrect_data_count() >= (int)ACCELMETER_APP_MAX_INCORRECT_DATA_INSTANCES)
                    {
                        aesdlog_err("STATE_WORKING_MEASURE: Invalid data received %d times", (int)ACCELMETER_APP_MAX_INCORRECT_DATA_INSTANCES);
                        timer_stop();
                        gnssdata_stop();
                        if (accelmeter_app_get_current_checkpoint() > 0.0)
                        {
                            /* We got some data */
                            aesdlog_dbg_info("STATE_WORKING_MEASURE: Some valid data received");
                            set_state(&sm_params.current_state, STATE_WORKING_ANALYZE);
                        } 
                        else
                        {
                            /* No valid data received */
                            aesdlog_err("STATE_WORKING_MEASURE: No valid data received");
                            accelmeter_app_stop();
                            send_status_data_to_client(&sm_params, "RUNNING_ERROR^");
                            set_state(&sm_params.current_state, STATE_DONE);
                        }
                    }
                }

                usleep(100);
            }
            break;
            case STATE_WORKING_ANALYZE:
            {
                double accel_time[] = {-1.0, -1.0, -1.0};
                int i;
                aesdlog_dbg_info("STATE_WORKING_ANALYZE: Analyzing data");
                accelmeter_app_accel_to_file(); /* Only if debug enabled */
                accelmeter_app_analyze_data(&accel_time[0], &accel_time[1], &accel_time[2]);
                accelmeter_app_stop();

                for (i = 0; i < 3; i++)
                {
                    char sendstr[100];
                    if (accel_time[i] < 0.0)
                    {
                        aesdlog_dbg_info("STATE_WORKING_ANALYZE: Checkpoint %d not reached", i);
                        snprintf(sendstr, sizeof(sendstr), "STATE_WORKING^RUNNING_TIMEOUT^%d#%d\n", i, (int)(ACCEL_TIMEOUT_S));
                    }
                    else
                    {
                        aesdlog_dbg_info("STATE_WORKING_ANALYZE: Checkpoint %d reached at time %.3lf", i, accel_time[i]);
                        snprintf(sendstr, sizeof(sendstr), "STATE_WORKING^RUNNING_STATUS^%d#%.2lf\n", i, accel_time[i]);
                    }

                    (void)send_to_client(&sm_params, sendstr);
                }

                (void)send_to_client(&sm_params, (char *)"STATE_WORKING^RUNNING_DONE^Finished\n");
                set_state(&sm_params.current_state, STATE_DONE);
            }
            break;
            case STATE_ABORT_REQUESTED:
            {
                /* Handle abort requested */
                sm_params.run_listener = FALSE;
                timer_stop();
                gnssdata_stop();
                accelmeter_app_stop();
                set_state(&sm_params.current_state, STATE_DONE);
                (void)send_to_client(&sm_params, (char *)"ABORTED\n");
            }
            break;
            case STATE_FINISHED:
            {
                /* Handle finished */
                aesdlog_dbg_info("STATE_FINISHED\n");
                set_state(&sm_params.current_state, STATE_INIT);
            }
                break;
            case STATE_DONE:
            {
                /* Handle done */
                aesdlog_dbg_info("State done");
                sm_params.run_listener = FALSE;
                pthread_join(listener_thread, NULL);
                set_state(&sm_params.current_state, STATE_FINISHED);
            }
                break;
            case STATE_ERROR:
            {
                /* Handle error */
                teardown();
                is_running = FALSE;
            }
                break;
            case STATE_UNEXPECTED_ERROR:
            {
                /* Handle unexpected error */
                teardown();
                is_running = FALSE;
            }
                break;
            default:
            {
                /* Handle unknown state */
                aesdlog_err("Unknown state %d", get_state(&sm_params.current_state));
                teardown();
                is_running = FALSE;
            }
                break;
        }
    }

    aesdlog_info("server_run: leaving state machine");
}

static void listener_task(void* arg)
{
    aesdlog_dbg_info("Started listener thread");
    serverapp_states cur_state;
    struct state_machine_params* params = (struct state_machine_params*)arg;
    while ((params->run_listener) == TRUE)
    {
        if (socket_connections_read_data_from_client(params->conf_fd, 
                                                     CLIENT_RECEIVE_TIMEOUT,
                                                     &params->in_buf) == TRUE)
        {
            cur_state = get_state(&params->current_state);
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
                if ((cur_state >= STATE_START_REQUESTED) &&
                    (cur_state <= STATE_WORKING_ANALYZE))
                    {
                        aesdlog_dbg_info("Received REQUEST_ABORT message");
                        set_state(&params->current_state, STATE_ABORT_REQUESTED);
                    }
            }
            else if (strcmp(params->in_buf, "REQUEST_STATUS\n") == 0)
            {
                free(params->in_buf);
                params->in_buf = NULL;
                if ((cur_state >= STATE_WORKING) && (cur_state <= STATE_WORKING_ANALYZE))
                {
                    aesdlog_dbg_info("Received REQUEST_STATUS message");
                    send_status_data_to_client(params, "");
                }
            }
            else if (strcmp(params->in_buf, "STATE_INIT\n") == 0)
            {
                free(params->in_buf);
                params->in_buf = NULL;
                aesdlog_dbg_info("Received STATE_INIT message");
                set_state(&params->current_state, STATE_START_REQUESTED);
            }
            else
            {
                free(params->in_buf);
                aesdlog_err("Received generic message: %s", params->in_buf);
            }
        }
        else
        {
            read_client_error(&params->current_state);
            params->run_listener = FALSE;
        }
    }

    aesdlog_info("gnssposget - closing listener_thread");
}

static void read_client_error(serverapp_states* current_state)
{
    aesdlog_err("Failed to read data from client");
    *current_state = STATE_UNEXPECTED_ERROR;
}

static void set_state(serverapp_states *state_var, serverapp_states new_state)
{
    pthread_mutex_lock(&state_mutex);
    *state_var = new_state;
    pthread_mutex_unlock(&state_mutex);
}

static serverapp_states get_state(serverapp_states *state_var)
{
    serverapp_states current_state;

    pthread_mutex_lock(&state_mutex);
    current_state = *state_var;
    pthread_mutex_unlock(&state_mutex);

    return current_state;
}

static Boolean send_to_client(struct state_machine_params* sm_params, char *buf)
{
    if (socket_connections_send_data_to_client(sm_params->conf_fd, &buf[0U]) == FALSE)
    {
        /* Something went terribly wrong */
        aesdlog_err("ERROR sending message to client");

        set_state(&sm_params->current_state, STATE_UNEXPECTED_ERROR);
        sm_params->run_listener = FALSE;
        return FALSE;
    }

    return TRUE;
}

static void send_status_data_to_client(struct state_machine_params* sm_params, char *additional_info)
{
    char *status_string;
    char sendstr[100];
    gnssdata_get_status_flag = TRUE;
    sleep(1);
    gnssdata_get_status_flag = FALSE;
    gnssdata_get_status(&status_string);
    sprintf(sendstr, "STATE_WORKING^%s%s\n", additional_info, status_string);
    free(status_string);
    (void)send_to_client(sm_params, sendstr);
}

static void teardown(void)
{
    gnssdata_stop();
    pthread_join(listener_thread, NULL);
    pthread_join(server_thread, NULL);
    pthread_mutex_destroy(&state_mutex);
}
