#ifndef ACCELMETER_APP
#define ACCELMETER_APP

#include "typedefs.h"


/* Consider that acceleration started from this speed */
#define ACCELMETER_APP_START_SPEED_THRESHOLD    (3.0)
/* Initial capacity for speed data storage */
#define ACCELMETER_APP_INITIAL_CAPACITY        (10U)
/* Number of allowed instances of incorrect data */
#define ACCELMETER_APP_MAX_INCORRECT_DATA_INSTANCES (5U)

extern void accelmeter_app_stop(void);
extern void accelmeter_app_start(void);
extern Boolean accelmeter_app_add_data(double timestamp, double speed);
extern int  accelmeter_app_get_data_size(void);
extern void accelmeter_app_handle_incorrect_data(void);
extern int  accelmeter_app_get_incorrect_data_count(void);
extern double accelmeter_app_get_current_checkpoint(void);
extern void accelmeter_app_set_checkpoint(double checkpoint);
extern void accelmeter_app_analyze_data(double *time1, double *time2, double *time3);

/* debug */
extern void accelmeter_app_accel_to_file(void);

#endif /* ACCELMETER_APP */
