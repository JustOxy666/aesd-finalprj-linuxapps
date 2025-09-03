#ifndef GNSSDATA_H
#define GNSSDATA_H

#include "typedefs.h"


extern void gnssdata_start(void);
extern void gnssdata_stop(void);
extern Boolean gnssdata_poll_status(void);
extern void gnssdata_get_status(char **buf);
extern double gnssdata_get_timestamp(void);
extern double gnssdata_get_speed(void);


extern Boolean gnssdata_get_status_flag;



#endif /* GNSSDATA_H */
