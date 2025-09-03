#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "typedefs.h"
#include "aesdlog.h"
#include "accelmeter-app.h"


/* ---------------------------------------------  */
/* Private macro declarations */
/* ---------------------------------------------  */


#define SPEED_DATA_CHUNK        (256U)
#define CHECKPOINT_1            (30.0)
#define CHECKPOINT_2            (60.0)
#define CHECKPOINT_3            (100.0)

/* Consecutive instances of speed to detect starting point */
#define JITTER_SMOOTH_COUNT     (5U)


/* ---------------------------------------------  */
/* Private types declarations */
/* ---------------------------------------------  */


typedef struct
{
    double timestamp;
    double speed;
} speed_data_chunk;

typedef struct
{
    speed_data_chunk *data;
    int               size;
    int               capacity;
    int               incorrect_data_count;
    Boolean           checkpoint1;
    Boolean           checkpoint2;
    Boolean           checkpoint3;
} acceleration_data;


/* ---------------------------------------------  */
/* static variables declarations */
/* ---------------------------------------------  */


static acceleration_data accel;
static Boolean is_running;

/* ---------------------------------------------  */
/* static functions declarations */
/* ---------------------------------------------  */


static void speed_data_add(acceleration_data *data, speed_data_chunk speed);
static void speed_data_init(acceleration_data *dataArray, size_t capacity);
static void speed_data_free(acceleration_data *dataArray);
static int get_starting_point(double threshold, int jitter_count);

/* ---------------------------------------------  */
/* Public functions */
/* ---------------------------------------------  */

void accelmeter_app_start(void)
{
    if (is_running == FALSE)
    {
        is_running = TRUE;
        speed_data_init(&accel, ACCELMETER_APP_INITIAL_CAPACITY);
        accel.incorrect_data_count = 0;
        accel.checkpoint1 = FALSE;
        accel.checkpoint2 = FALSE;
        accel.checkpoint3 = FALSE;
    }
}

void accelmeter_app_stop(void)
{
    if (is_running == TRUE)
    {
        is_running = FALSE;
        speed_data_free(&accel);
    }
}

Boolean accelmeter_app_add_data(double timestamp, double speed)
{
    Boolean result = FALSE;
    if (is_running == TRUE)
    {
        speed_data_chunk chunk = {timestamp, speed};
        if ((accel.size > 0) && (timestamp == accel.data[accel.size - 1].timestamp))
        {
            /* Not adding data with the same timestamp as previous element */
            result = FALSE;
        }
        else
        {
            speed_data_add(&accel, chunk);
            result = TRUE;
        }
    }

    return result;
}

int accelmeter_app_get_data_size(void)
{
    return accel.size;
}

void accelmeter_app_handle_incorrect_data(void)
{
    accel.incorrect_data_count++;
}

int accelmeter_app_get_incorrect_data_count(void)
{
    return accel.incorrect_data_count;
}

double accelmeter_app_get_current_checkpoint(void)
{
    double current_checkpoint;
    if (accel.checkpoint1 == FALSE)
    {
        current_checkpoint = (double)CHECKPOINT_1;
    }
    else if (accel.checkpoint2 == FALSE)
    {
        current_checkpoint = (double)CHECKPOINT_2;
    }
    else if (accel.checkpoint3 == FALSE)
    {
        current_checkpoint = (double)CHECKPOINT_3;
    }
    else
    {
        /* No checkpoints left */
        current_checkpoint = 0.0;
    }

    return current_checkpoint;
}

void accelmeter_app_set_checkpoint(double checkpoint)
{
    if (checkpoint == (double)CHECKPOINT_1)
    {
        accel.checkpoint1 = TRUE;
    }
    else if (checkpoint == (double)CHECKPOINT_2)
    {
        accel.checkpoint2 = TRUE;
    }
    else if (checkpoint == (double)CHECKPOINT_3)
    {
        accel.checkpoint3 = TRUE;
    }
}

void accelmeter_app_analyze_data(double *time1, double *time2, double *time3)
{
    int start_index = 0;
    double start_timestamp = 0.0, cur_checkpoint = 0.0;
    int idx = 0;

    *time1 = *time2 = *time3 = -1;
    if (is_running == TRUE)
    {
        if (accel.checkpoint1 == TRUE)
        {
            /* Analyze data for checkpoint 1 */
            start_index = get_starting_point(ACCELMETER_APP_START_SPEED_THRESHOLD, JITTER_SMOOTH_COUNT);
            if (start_index >= 0)
            {
                /* Perform analysis on the data starting from start_index */
                start_timestamp = accel.data[start_index].timestamp;
                cur_checkpoint = (double)CHECKPOINT_1;
                for (idx = start_index; idx < accel.size; idx++)
                {
                    if (accel.data[idx].speed > cur_checkpoint)
                    {
                       if (cur_checkpoint == (double)CHECKPOINT_1)
                       {
                           *time1 = accel.data[idx].timestamp - start_timestamp;
                            aesdlog_dbg_info("accelmeter_app_analyze_data: checkpoint 1 time=%.3lf", *time1);
                           cur_checkpoint = (double)CHECKPOINT_2;
                       }
                       else if (cur_checkpoint == (double)CHECKPOINT_2)
                       {
                            *time2 = accel.data[idx].timestamp - start_timestamp;
                            aesdlog_dbg_info("accelmeter_app_analyze_data: checkpoint 2 time=%.3lf", *time2);
                            cur_checkpoint = (double)CHECKPOINT_3;
                       }
                       else if (cur_checkpoint == (double)CHECKPOINT_3)
                       {
                            *time3 = accel.data[idx].timestamp - start_timestamp;
                            aesdlog_dbg_info("accelmeter_app_analyze_data: checkpoint 3 time=%.3lf", *time3);
                            break;
                       }
                    }
                }
            }
            else
            {
                aesdlog_err("accelmeter_app_analyze_data: Couldn't find starting point");
            }
        }
        else
        {
            aesdlog_err("accelmeter_app_analyze_data: Function called while no checkpoints reached");
        }
    }
}

void accelmeter_app_accel_to_file(void)
{
#ifdef DEBUG_ON
    FILE *file;
    aesdlog_dbg_info("accelmeter_app_accel_to_file: Writing acceleration data to file");
    file = fopen("/home/root/acceleration_data.txt", "a");
    if (!file) {
        aesdlog_err("accelmeter_app_accel_to_file: Couldn't open file");
        return;
    }

    fprintf(file, "------------------------------------------------\n");
    fprintf(file, "------------------------------------------------\n");
    fprintf(file, "------------------------------------------------\n");
    fprintf(file, "Acceleration Data:\n");
    for (int i = 0; i < accel.size; i++) {
        fprintf(file, "Timestamp: %.3lf, Speed: %.3lf\n",
                accel.data[i].timestamp, accel.data[i].speed);
    }

    fclose(file);
#endif /* DEBUG_ON */
}



/* ---------------------------------------------  */
/* Private functions */
/* ---------------------------------------------  */


/* Calculates acceleration starting point
*
*  @param threshold The speed threshold to consider
*  @param jitter_count The number of consecutive samples above the threshold
*
*  @return The index of the starting point or -1 if not found
*/
static int get_starting_point(double threshold, int jitter_count)
{
    aesdlog_dbg_info("accelmeter_app_get_starting_point: Started searching for starting point");
    int idx, start_index = -1, temp_index, hits = 0;
    for (idx = 0; idx < accel.size; idx++)
    {
        if (accel.data[idx].speed > threshold) 
        {
            hits++;
            if (hits >= jitter_count)
            {
                start_index = idx;
                break;
            }
        }
        else 
        {
            hits = 0;
        }
    }

    aesdlog_dbg_info("accelmeter_app_get_starting_point: Found starting point at index %d", start_index);
    if (start_index > 2)
    {
        aesdlog_dbg_info("accelmeter_app_get_starting_point: Trying to refine starting point");
        /* Found a starting point, try to make it more precise */
        hits = 0;
        temp_index = start_index;
        jitter_count = 2;
        for(idx = (temp_index - 1); idx >= 0; idx--)
        {
            if (accel.data[idx].speed > accel.data[idx + 1].speed)
            {
                hits++;
                if (hits >= jitter_count)
                {
                    break;
                }
            }
            else
            {
                start_index--;
            }
        }

        aesdlog_dbg_info("accelmeter_app_get_starting_point: Refined starting point at index %d", start_index);
    }

    return start_index;
}

static void speed_data_init(acceleration_data *dataArray, size_t capacity)
{
    dataArray->data = malloc(capacity * sizeof(speed_data_chunk));
    if (!dataArray->data) {
        aesdlog_err("malloc: %s", strerror(errno));
        exit(1);
    }

    dataArray->size = 0;
    dataArray->capacity = capacity;
}

static void speed_data_add(acceleration_data *dataArray, speed_data_chunk speed)
{
    if (dataArray->size == dataArray->capacity)
    {
        size_t new_capacity = dataArray->capacity * 2;
        speed_data_chunk *tmp = realloc(dataArray->data, new_capacity * sizeof(speed_data_chunk));
        if (!tmp)
        {
            aesdlog_err("realloc: %s", strerror(errno));
            exit(1);
        }

        dataArray->data = tmp;
        dataArray->capacity = new_capacity;
    }

    dataArray->data[dataArray->size] = speed;
    dataArray->size++;
}

static void speed_data_free(acceleration_data *dataArray)
{
    free(dataArray->data);
    dataArray->data = NULL;
    dataArray->size = 0;
    dataArray->capacity = 0;
}
