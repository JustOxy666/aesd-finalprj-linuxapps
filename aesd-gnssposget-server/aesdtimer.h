#ifndef AESTIMER_H
#define AESTIMER_H

#include "typedefs.h"

extern void timer_start(void);
extern void timer_stop(void);
extern Boolean timer_is_elapsed(int timeout);

#endif /* AESTIMER_H */
