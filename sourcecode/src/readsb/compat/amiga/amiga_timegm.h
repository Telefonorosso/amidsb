#ifndef READSB_AMIGA_TIMEGM_H
#define READSB_AMIGA_TIMEGM_H
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif
time_t readsb_amiga_timegm(const struct tm *utc);
#ifdef __cplusplus
}
#endif
#endif
