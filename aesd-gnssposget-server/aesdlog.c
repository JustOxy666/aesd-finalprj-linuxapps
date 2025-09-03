#include <syslog.h>
#include <stdarg.h>
#include <stdlib.h>

#include "aesdlog.h"

void aesdlog_init(void)
{
    openlog(NULL, LOG_NDELAY, LOG_USER);
}

void aesdlog_info(const char *message, ...)
{
    va_list args;
    va_start(args, message);
    vsyslog(LOG_INFO, message, args);
    va_end(args);
}

void aesdlog_dbg_info(const char *message, ...)
{
#ifdef DEBUG_ON
    va_list args;
    va_start(args, message);
    vsyslog(LOG_DEBUG, message, args);
    va_end(args);
#endif /* DEBUG_ON */
}

void aesdlog_err(const char *message, ...)
{
    va_list args;
    va_start(args, message);
    vsyslog(LOG_ERR, message, args);
    va_end(args);
}
