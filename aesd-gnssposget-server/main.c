/* -------------------------------------------
** 
** 
** This is a server of gnssposget application
** 
** 
** -------------------------------------------  */


#include <sys/socket.h> /* sockaddr_in */
#include <sys/types.h>
#include <syslog.h>
#include <signal.h> /* signal handling */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "gnssposget-server.h"
#include "socket_connections.h"
#include "typedefs.h"

#define DAEMON_ARG                  ("-d")


void parse_args(int argc, char** argv);
void signalHandler(int);

static Boolean is_daemon = FALSE;



int main(int argc, char** argv)
{
    struct sigaction signal_action;
    int listen_fd;

    /* Handle argument(s) */
    parse_args(argc, argv);

    /* Init syslog */
    openlog(NULL, LOG_NDELAY, LOG_USER);

    /* Setup SIGINT & SIGTERM callbacks */
    memset(&signal_action, 0, sizeof(signal_action));
    signal_action.sa_handler = signalHandler;
    sigaction(SIGTERM, &signal_action, NULL);
    sigaction(SIGINT, &signal_action, NULL);

    /* Setup things and get socket file descriptor */
    socket_connections_setup(&listen_fd, is_daemon);

    //pthread_create(&threads[0], NULL, (void*)&timestamp_task, NULL);

    /* Run GNSS Position Get Server main loop (runs forever) */
    gnssposget_server_mainloop(&listen_fd);

    gnssposget_server_request_teardown();
    socket_connections_teardown();
    return 0;
}


void parse_args(int argc, char** argv)
{
    if (argc == 2)
    {
        if (strcmp(argv[1], DAEMON_ARG) == 0)
        {
#ifdef DEBUG_ON           
            printf("Running in daemon mode\n");
#endif /* DEBUG_ON */
            is_daemon = TRUE;
        }
        else
        {
            printf("Invalid argument!\n");
            exit(0);
        }
    }
    else if (argc > 2)
    {
        printf("Too many arguments!\n");
        exit(-1);
    }
    else
    {
        // all good
    }
}


void signalHandler(int signal_number)
{
#ifdef DEBUG_ON
    printf("caught signal %d\n", signal_number);
#endif /* DEBUG_ON */
    syslog(LOG_INFO, "Caught signal, exiting...");
    gnssposget_server_request_teardown();
    socket_connections_teardown();
}
