#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h> /* memory allocate */
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/tty.h>   // for TIOCSETD
#include <errno.h>

#include <pthread.h>
#include <syslog.h>

#include "accelmeter-app.h"
#include "typedefs.h"

/* Length of NMEA address ($GPXXX) */
#define NMEA_ADDR_LEN               (6U)
/* Index inside GSV NMEA of: number of satellites in view */
#define GSV_INDEX_SAT_COUNT         (3U)
/* Index inside GSV NMEA of: signal strength */
#define GSV_INDEX_ANT_STR           (7U)
/* Index inside RMC NMEA of: UTC time */
#define RMC_INDEX_TIME              (1U)
/* Expected length of UTC time field */
#define RMC_TIME_LEN                (9U)
/* Index of seconds inside UTC time field */
#define RMC_TIME_SECONDS_INDEX      (4U)
/* Index inside RMC NMEA of: Status, V = Navigation receiver warning, A = Data valid */
#define RMC_INDEX_FIX_STAT          (2U)
/* Index inside RMC NMEA of: Speed over ground (knots) */
#define RMC_INDEX_SPEED_K           (7U)
/* Index inside TXT NMEA of: Any ASCII text */
#define TXT_INDEX_TEXT              (4U)

#define UART_DEVICE                 ("/dev/ttyAMA1")
/* aesd-gnssposget-driver TTY Line Discipline number */
#define N_GNSSPOSGET                (20)


/* ---------------------------------------------  */
/* Private types declarations */
/* ---------------------------------------------  */

typedef enum
{
    /* Text message (statuses etc) */
    NMEA_TXT,
    /* Time, coordinates, speed, fix status */
    NMEA_RMC,
    /* Nr of sattelites, signal strength */
    NMEA_GSV
} nmea_types;

char status_string[80] = "Fix status: NA, Sattelites in view: NA, Signal strength: NA";
struct status_packet
{
    Boolean fix_valid; /* Determine signal validity only based on this */
    Boolean sats_valid;
    Boolean ant_valid;
    /* Fixed length status string. Example: "Fix status: NA, Sattelites in view: NA, Signal strength: NA" */
    char sats_nr[10];
    char ant_strength[10];
};

struct speed_packet
{
    double timestamp;
    double speed;
};


/* ---------------------------------------------  */
/* Public variables declarations */
/* ---------------------------------------------  */


 Boolean accelmeter_app_get_status_flag;


/* ---------------------------------------------  */
/* Private variables declarations */
/* ---------------------------------------------  */


static Boolean run_listener;
static pthread_mutex_t nmea_buf_mutex;
static pthread_mutex_t status_mutex;
static pthread_mutex_t speed_mutex;
static pthread_t listener_thread;
static struct status_packet cur_status = 
{
    .fix_valid = FALSE,
    .sats_valid = FALSE,
    .ant_valid = FALSE,
    .sats_nr = "NA",
    .ant_strength = "NA"
};
static struct speed_packet cur_speed =
{
    .timestamp = -1.0,
    .speed = -1.0
};

static const char gptxt[] = "$GPTXT";
static const char gpgsv[] = "$GPGSV";
static const char gprmc[] = "$GPRMC";

static const char *testnmea[] =
{
    "$GPRMC,223654.00,A,5924.30996,N,02437.73543,E,1.027,81.41,240825,,,A*50",
    "$GPGSV,2,1,06,11,46,083,,12,51,118,14,28,46,286,37,29,45,224,42*77",
    "$GPRMC,223656.00,A,5924.30995,N,02437.73494,E,0.463,,240825,,,A*7D",
    "$GPGSV,2,2,06,31,25,312,31,32,08,250,37*72",
    "$GPRMC,223657.00,A,5924.30992,N,02437.73464,E,0.343,,240825,,,A*71",
    "$GPGSV,2,1,06,11,46,083,,12,51,118,14,28,46,286,38,29,45,224,42*78",
    "$GPRMC,223658.00,A,5924.30993,N,02437.73432,E,0.863,,240825,,,A*75",
    "$GPGSV,2,2,06,31,25,312,31,32,08,250,38*7D",
    "$GPRMC,223659.00,A,5924.31000,N,02437.73435,E,1.472,77.94,240825,,,A*5F"
};

// static const char *testnmea = "$GPRMC,223654.00,A,5924.30996,N,02437.73543,E,1.027,81.41,240825,,,A*50";

/* ---------------------------------------------  */
/* Private functions declarations */
/* ---------------------------------------------  */
static void read_data_task(void*);
static void extract_nmea(char* buf);
static void populate_status(void);

/* ---------------------------------------------  */
/* Public functions */
/* ---------------------------------------------  */
Boolean accelmeter_app_poll_status(void)
{
    Boolean result;
    pthread_mutex_lock(&status_mutex);
    result = cur_status.fix_valid;
    pthread_mutex_unlock(&status_mutex);

    return result;
}

void accelmeter_app_get_status(char **buf)
{
    pthread_mutex_lock(&status_mutex);
    *buf = calloc(sizeof(status_string), sizeof(char));
    (void)memcpy((char*)*buf, (char*)status_string, sizeof(status_string));
    pthread_mutex_unlock(&status_mutex);
    /* TODO: free status in gnssposget-server */
}

double accelmeter_app_get_timestamp(void)
{
    double ret;
    pthread_mutex_lock(&speed_mutex);
    ret = cur_speed.timestamp;
    pthread_mutex_unlock(&speed_mutex);
    return ret;
}

double accelmeter_app_get_speed(void)
{
    double ret;
    pthread_mutex_lock(&speed_mutex);
    ret = cur_speed.speed;
    pthread_mutex_unlock(&speed_mutex);
    return ret;
}

void accelmeter_app_start()
{
    run_listener = TRUE;
    accelmeter_app_get_status_flag = TRUE;
    pthread_mutex_init(&nmea_buf_mutex, NULL);
    pthread_mutex_init(&status_mutex, NULL);
    pthread_mutex_init(&speed_mutex, NULL);

    syslog(LOG_INFO, "accelmeter_app_start(): Starting listener thread");
    pthread_create(&listener_thread, NULL, (void*)read_data_task, (void*)&run_listener);
}

void accelmeter_app_stop()
{
    run_listener = FALSE;

    pthread_join(listener_thread, NULL);
    pthread_mutex_destroy(&nmea_buf_mutex);
    pthread_mutex_destroy(&status_mutex);
    pthread_mutex_destroy(&speed_mutex);
    syslog(LOG_INFO, "accelmeter - leaving accelmeter_app_stop()");
}

/* ---------------------------------------------  */
/* Private functions */
/* ---------------------------------------------  */
static void read_data_task(void* arg)
{
    int *run_flag = arg;
    int fd;
    int ldisc = N_GNSSPOSGET;
    char buffer[256];

    fd = open(UART_DEVICE, O_RDONLY | O_NOCTTY);
    if (fd < 0) {
        perror("open");
        *run_flag = FALSE;
    }

    if (ioctl(fd, TIOCSETD, &ldisc) < 0) {
        perror("ioctl(TIOCSETD)");
        close(fd);
        *run_flag = FALSE;
    }

    syslog(LOG_INFO, "read_data_task(): Attached line discipline %d to %s\n", ldisc, UART_DEVICE);
    while(1)
    {
        if (*run_flag == FALSE)
        {
            break;
        }

        pthread_mutex_lock(&nmea_buf_mutex);
        int ret = read(fd, (char *)(buffer), sizeof(buffer));
        if (ret < 0)
        {
            perror("read():");
            *run_flag = FALSE;
        }
        else if (ret == 0)
        {
            syslog(LOG_INFO, "read_data_task(): received nothing");
        }
        else
        {
            buffer[ret] = '\0';
            pthread_mutex_unlock(&nmea_buf_mutex);
            extract_nmea(buffer);
        }
    }
    
    syslog(LOG_INFO, "accelmeter-app - closing listener_thread");
}

static void extract_nmea(char* buf)
{
    nmea_types nmea_type;
    char* parsed_buf, *parsed_buf_ptr, *token;
    unsigned int index = 0;
    pthread_mutex_lock(&nmea_buf_mutex);
    parsed_buf = calloc(strlen(buf) + 1, (sizeof(char)));
    (void)strcpy((char*)parsed_buf, (char*)buf);
    pthread_mutex_unlock(&nmea_buf_mutex);

    /* Check which message we got */
    parsed_buf_ptr = parsed_buf;
    if (strncmp(parsed_buf, gpgsv, NMEA_ADDR_LEN) == 0)
    {
        /* -------------------------------------------------------- */
        /* Parse GSV */
        /* -------------------------------------------------------- */
        nmea_type = NMEA_GSV;

        /* Just skip this message if status is not needed */
        pthread_mutex_lock(&status_mutex);
        if (accelmeter_app_get_status_flag == TRUE)
        {
            while((token = (strsep(&parsed_buf_ptr, ","))) != NULL)
            {
                if (index == GSV_INDEX_SAT_COUNT)
                {
                    cur_status.sats_valid = FALSE;
                    if (*token != '\0')
                    {
                        (void)memcpy((char*)cur_status.sats_nr, (char*)token, sizeof(token));
                        cur_status.sats_valid = TRUE;
                    }
                }
                else if (index == GSV_INDEX_ANT_STR)
                {
                    cur_status.ant_valid = FALSE;
                    if (*token != '\0')
                    {
                        (void)memcpy((char*)cur_status.ant_strength, (char*)token, sizeof(token));
                        cur_status.ant_valid = TRUE;
                    }

                    /* We can break after this one */
                    break;
                }
                else
                {
                    /* Do nothing */
                }

                index++;
            }

            populate_status();
        }

        pthread_mutex_unlock(&status_mutex);
        free(parsed_buf);
    }
    
    else if (strncmp(parsed_buf, gprmc, NMEA_ADDR_LEN) == 0)
    {
        /* -------------------------------------------------------- */
        /* Parse RMC */
        /* -------------------------------------------------------- */
        nmea_type = NMEA_RMC;

        while((token = (strsep(&parsed_buf_ptr, ","))) != NULL)
        {
            if (index == RMC_INDEX_TIME)
            {
                pthread_mutex_lock(&speed_mutex);
                cur_speed.timestamp = -1.0;
                if (*token != '\0')
                {
                    if (strlen(token) == RMC_TIME_LEN)
                    {
                        /* TODO: Get timestamp in hhmmss.ss */
                        if ((cur_speed.timestamp = atof(&token[RMC_TIME_SECONDS_INDEX])) <= 0)
                        {
                            cur_speed.timestamp = -1.0;
                            syslog(LOG_INFO, "Timestamp Invalid!: %s", token);
                            /* Timestamp invalid */
                        }
                    }
                }

                pthread_mutex_unlock(&speed_mutex);
            }
            else if (index == RMC_INDEX_FIX_STAT)
            {
                pthread_mutex_lock(&status_mutex);
                if (accelmeter_app_get_status_flag == TRUE)
                {
                    cur_status.fix_valid = FALSE;
                    if (*token != '\0')
                    {
                        /* A=Autonomous GNSS Fix, D=Differential GNSS Fix */
                        if ((*token == 'A') || (*token == 'D'))
                        {
                            cur_status.fix_valid = TRUE;
                        }
                    }

                    populate_status();
                }

                pthread_mutex_unlock(&status_mutex);
            }
            else if (index == RMC_INDEX_SPEED_K)
            {
                pthread_mutex_lock(&speed_mutex);
                cur_speed.speed = -1.0;
                if (*token != '\0')
                {
                    if ((cur_speed.speed = atof(token)) <= 0)
                    {
                        cur_speed.speed = -1.0;
                        syslog(LOG_INFO, "Speed Invalid!: %s", token);
                        /* Speed invalid */
                    }
                }

                pthread_mutex_unlock(&speed_mutex);
            }
            else
            {
                /* Do nothing */
            }

            index++;
        }
    }
    else if (strncmp(parsed_buf, gprmc, NMEA_ADDR_LEN) == 0)
    {
        /* -------------------------------------------------------- */
        /* Parse TXT */
        /* -------------------------------------------------------- */
        nmea_type = NMEA_TXT;

        while((token = (strsep(&parsed_buf_ptr, ","))) != NULL)
        {
            if (index == TXT_INDEX_TEXT)
            {
                if (*token != '\0')
                {
                    // TODO: memcpy text to buffer
                }
            }

            index++;
        }
    }
    else
    {
        /* We received meesage that was not expected! */
        syslog(LOG_PERROR, "ERROR! Unsupported message received from GNSS: %s", parsed_buf);
        accelmeter_app_stop();
    }
}

static void populate_status()
{
    sprintf(status_string, "%s%s%s%s%s%s", 
        "Fix status: ",
        ((cur_status.fix_valid == TRUE) ? "OK" : "NA"),
        ", Sattelites in view: ",
        ((cur_status.sats_valid == TRUE) ? cur_status.sats_nr : "NA"),
        ", Signal strength: ",
        ((cur_status.ant_valid == TRUE) ? cur_status.ant_strength : "NA"));
}

