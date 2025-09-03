#include <time.h>
#include <unistd.h>
#include <stdio.h>

#include "typedefs.h"
#include "aesdtimer.h"


typedef struct
{
    struct timespec start;
    struct timespec now;
    Boolean is_running;
} time_struct;


static double timer_poll(void);


time_struct timer;


void timer_start(void)
{
    if(timer.is_running == FALSE)
    {
        clock_gettime(CLOCK_MONOTONIC_RAW, &timer.start);
        timer.is_running = TRUE;
    }
}

void timer_stop(void)
{
    if(timer.is_running == TRUE)
    {
        timer.is_running = FALSE;
    }
}

Boolean timer_is_elapsed(int timeout)
{
    Boolean result = FALSE;
    if ((timer.is_running == TRUE) && (timer_poll() > timeout))
    {
        result = TRUE;
    }

    return result;
}

static double timer_poll(void)
{
    double elapsed = 0;
    if (timer.is_running == TRUE)
    {
        clock_gettime(CLOCK_MONOTONIC_RAW, &timer.now);
        elapsed = (timer.now.tv_sec - timer.start.tv_sec) +
                  (timer.now.tv_nsec - timer.start.tv_nsec) / 1e9;
    }

    return elapsed;
}
