#ifndef READSB_LITE_ENGINE_H
#define READSB_LITE_ENGINE_H
#include <stddef.h>
#include <stdint.h>
/* Original readsb DSP engine: common to native file and RF-burst inputs. */
void lite_engine_set_quiet(int enabled);
int lite_engine_begin(void);
int lite_engine_feed(const unsigned char *iq, size_t nbytes);
void lite_engine_end(int status);
uint64_t lite_engine_decoded_total(void);
uint64_t lite_engine_sample_clock_ms(void);
uint64_t lite_engine_preambles_total(void);
uint64_t lite_engine_rejected_total(void);
/* RF entry point is only built on AmigaOS. */
int lite_rtl_run(const char *mode, int gain_manual, int gain_tenths_db);
#endif
