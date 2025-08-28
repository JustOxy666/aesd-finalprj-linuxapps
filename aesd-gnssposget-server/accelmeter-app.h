#ifndef ACCELMETER_APP
#define ACCELMETER_APP

#include "typedefs.h"


extern void accelmeter_app_start(void);
extern void accelmeter_app_stop(void);
extern Boolean accelmeter_app_poll_status(void);
extern void accelmeter_app_get_status(char **buf);
extern double accelmeter_app_get_timestamp(void);
extern double accelmeter_app_get_speed(void);


extern Boolean accelmeter_app_get_status_flag;







#endif /* ACCELMETER_APP */
