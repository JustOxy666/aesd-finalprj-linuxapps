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

#include "accelmeter-app.h"
#include "typedefs.h"
#include "aesdlog.h"
#include "gnssdata.h"

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

#define GNSS_MODULE_START_PATH      ("/usr/bin/gnss_module_start.sh")
#define UART_DEVICE                 ("/dev/ttyAMA1")
/* aesd-gnssposget-driver TTY Line Discipline number */
#define N_GNSSPOSGET                (20)


/* ---------------------------------------------  */
/* Private types declarations */
/* ---------------------------------------------  */


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


 Boolean gnssdata_get_status_flag;


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



/* ---------------------------------------------  */
/* Private functions declarations */
/* ---------------------------------------------  */
static void read_data_task(void*);
static void extract_nmea(char *buf);
static void populate_status(void);
static double parse_utc_to_seconds(const char *utc_str);

/* ---------------------------------------------  */
/* Public functions */
/* ---------------------------------------------  */
Boolean gnssdata_poll_status(void)
{
    Boolean result;
    pthread_mutex_lock(&status_mutex);
    result = cur_status.fix_valid;
    pthread_mutex_unlock(&status_mutex);

    return result;
}

void gnssdata_get_status(char **buf)
{
    pthread_mutex_lock(&status_mutex);
    *buf = malloc(strlen(status_string) + 1);
    (void)strcpy((char*)*buf, (char*)status_string);
    pthread_mutex_unlock(&status_mutex);
}

double gnssdata_get_timestamp(void)
{
    double ret;
    pthread_mutex_lock(&speed_mutex);
    ret = cur_speed.timestamp;
    pthread_mutex_unlock(&speed_mutex);
    return ret;
}

double gnssdata_get_speed(void)
{
    double ret;
    pthread_mutex_lock(&speed_mutex);
    ret = cur_speed.speed;
    pthread_mutex_unlock(&speed_mutex);
    ret *= 1.852; /* Convert from knots to km/h */
    return ret;
}

void gnssdata_start()
{
    aesdlog_dbg_info("gnssdata_start");
    run_listener = TRUE;
    gnssdata_get_status_flag = TRUE;
    pthread_mutex_init(&nmea_buf_mutex, NULL);
    pthread_mutex_init(&status_mutex, NULL);
    pthread_mutex_init(&speed_mutex, NULL);

    aesdlog_dbg_info("gnssdata_start(): Starting listener thread");
    pthread_create(&listener_thread, NULL, (void*)read_data_task, (void*)&run_listener);
}

void gnssdata_stop()
{
    aesdlog_dbg_info("gnssdata_stop");
    run_listener = FALSE;

    gnssdata_get_status_flag = FALSE;
    pthread_join(listener_thread, NULL);
    pthread_mutex_destroy(&nmea_buf_mutex);
    pthread_mutex_destroy(&status_mutex);
    pthread_mutex_destroy(&speed_mutex);
    aesdlog_info("accelmeter - leaving gnssdata_stop()");
}

/* ---------------------------------------------  */
/* Private functions */
/* ---------------------------------------------  */
static void read_data_task(void* arg)
{
    int *run_flag = arg;
    int fd;
    int read_count = 0;
    int ldisc = N_GNSSPOSGET;
    char buffer[256];

    aesdlog_dbg_info("Setting up UART port %s", UART_DEVICE);
    if (system(GNSS_MODULE_START_PATH) != 0) {
        aesdlog_err("Failed to set up GNSS module");
        *run_flag = FALSE;
    }

    aesdlog_dbg_info("Opening UART port %s", UART_DEVICE);
    fd = open(UART_DEVICE, O_RDONLY | O_NOCTTY);
    if (fd < 0) {
        aesdlog_err("open: %s", strerror(errno));
        *run_flag = FALSE;
    }

    aesdlog_dbg_info("Attaching to TTY Line Discipline");
    if (ioctl(fd, TIOCSETD, &ldisc) < 0) {
        aesdlog_err("ioctl(TIOCSETD): %s", strerror(errno));
        close(fd);
        *run_flag = FALSE;
    }

    aesdlog_info("read_data_task(): Attached line discipline %d to %s", ldisc, UART_DEVICE);
    while(1)
    {
        if (*run_flag == FALSE)
        {
            break;
        }

        pthread_mutex_lock(&nmea_buf_mutex);
        int ret = read(fd, (char *)(buffer + read_count), (sizeof(buffer) - read_count));
        if (ret < 0)
        {
            aesdlog_err("UART read data error: %s", strerror(errno));
            *run_flag = FALSE;
            pthread_mutex_unlock(&nmea_buf_mutex);
        }
        else if (ret == 0)
        {
            aesdlog_info("read_data_task(): received nothing");
            *run_flag = FALSE;
            pthread_mutex_unlock(&nmea_buf_mutex);
        }
        else
        {
            /* Check for end of NMEA sentence (ret - 1 = \0) */
            if ((buffer[read_count + (ret - 2)] == '\r') || (buffer[read_count + (ret - 2)] == '\n'))
            {
                /* Default case: stop reading before start of checksum */
                char *checksum_start = strchr((const char *)buffer, '*');
                if (checksum_start)
                {
                    /* End string before checksum symbol */
                    *checksum_start = '\0';
                }

                pthread_mutex_unlock(&nmea_buf_mutex);
                extract_nmea(buffer);
                read_count = 0;
            }
            else
            {
                // aesdlog_dbg_info("buffer without trailing \\r or \\n");
                read_count += ret;
                pthread_mutex_unlock(&nmea_buf_mutex);
            }
        }
    }
    
    close(fd);
    aesdlog_info("accelmeter-app - closing listener_thread");
}

static void extract_nmea(char *buf)
{
    char *parsed_buf, *parsed_buf_ptr, *token;
    unsigned int index = 0;
    pthread_mutex_lock(&nmea_buf_mutex);
    int buf_len = strlen(buf);
    parsed_buf = malloc(buf_len + 1);
    (void)strcpy((char*)parsed_buf, (char*)buf);
    pthread_mutex_unlock(&nmea_buf_mutex);

    /* Check which message we got */
    parsed_buf_ptr = parsed_buf;
    // aesdlog_dbg_info("*parsed_buf=%s", parsed_buf);
    if (strncmp(parsed_buf, gpgsv, NMEA_ADDR_LEN) == 0)
    {
        /* -------------------------------------------------------- */
        /* Parse GSV */
        /* -------------------------------------------------------- */
        /* Just skip this message if status is not needed */
        pthread_mutex_lock(&status_mutex);
        if (gnssdata_get_status_flag == TRUE)
        {
            while((token = (strsep(&parsed_buf_ptr, ","))) != NULL)
            {
                if (index == GSV_INDEX_SAT_COUNT)
                {
                    cur_status.sats_valid = FALSE;
                    if (*token != '\0')
                    {
                        (void)strcpy((char*)cur_status.sats_nr, (char*)token);
                        cur_status.sats_valid = TRUE;
                    }
                }
                else if (index == GSV_INDEX_ANT_STR)
                {
                    cur_status.ant_valid = FALSE;
                    if (*token != '\0')
                    {
                        (void)strcpy((char*)cur_status.ant_strength, (char*)token);
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
    }
    
    else if (strncmp(parsed_buf, gprmc, NMEA_ADDR_LEN) == 0)
    {
        /* -------------------------------------------------------- */
        /* Parse RMC */
        /* -------------------------------------------------------- */
        while((token = (strsep(&parsed_buf_ptr, ","))) != NULL)
        {
            if (index == RMC_INDEX_TIME)
            {
                pthread_mutex_lock(&speed_mutex);
                cur_speed.timestamp = -1.0;
                if (strlen(token) == RMC_TIME_LEN)
                {
                    cur_speed.timestamp = parse_utc_to_seconds(token);
                }

                pthread_mutex_unlock(&speed_mutex);
            }
            else if (index == RMC_INDEX_FIX_STAT)
            {
                pthread_mutex_lock(&status_mutex);
                if (gnssdata_get_status_flag == TRUE)
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
                        aesdlog_err("Speed Invalid!: %s", token);
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
    else if (strncmp(parsed_buf, gptxt, NMEA_ADDR_LEN) == 0)
    {
        /* -------------------------------------------------------- */
        /* Parse TXT */
        /* -------------------------------------------------------- */

        while((token = (strsep(&parsed_buf_ptr, ","))) != NULL)
        {
            if (index == TXT_INDEX_TEXT)
            {
                if (*token != '\0')
                {
                    /* Got TXT message. Send it to syslog */
                    aesdlog_info("Received text message from GNSS: %s", token);
                }
            }

            index++;
        }
    }
    else
    {
        /* We received meesage that was not expected! */
        free(parsed_buf);
        aesdlog_err("ERROR! Unsupported message received from GNSS: %s", parsed_buf);
        gnssdata_stop();
    }

    free(parsed_buf);
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

static double parse_utc_to_seconds(const char *utc_str) 
{
    int hh = 0, mm = 0;
    double ss = 0.0;

    /* Parse string in format "hhmmss.ss" */
    if (sscanf(utc_str, "%2d%2d%lf", &hh, &mm, &ss) != 3) {
        return -1.0; // error
    }

    /* Convert to total seconds since midnight */
    return hh * 3600.0 + mm * 60.0 + ss;
}
