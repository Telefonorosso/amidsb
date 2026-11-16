/* PORT38 FIX1: RF gain option parser, no OS or CRT dependencies. */
#ifndef LITE_GAIN_CLI_H
#define LITE_GAIN_CLI_H
struct lite_gain_option {
    int manual;        /* 0 = tuner analog automatic gain control */
    int tenths_db;     /* used only when manual is 1 */
};
/* Accepts GAIN=AUTO, or GAIN=0..49.6 (one optional decimal). */
int lite_gain_parse(const char *arg, struct lite_gain_option *out);
#endif
