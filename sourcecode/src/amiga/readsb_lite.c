/* readsb-Amiga PORT36 Lite RF async PIPE probe; original PORT33 DSP unchanged.
 * Combines unchanged readsb demod_2400.c, crc.c, mode_s.c, convert.c.
 * Supports native Poseidon RF via rf1090_backend.c and file UC8 input.
 * GPL-3.0-or-later; same licensing terms as readsb demod_2400.
 */
#include "readsb.h"
#include "convert.h"
#include "lite_engine.h"
#include "gain_cli.h"
#include "lite_interactive.h"
#if defined(READSB_LITE_AMIPORT) || defined(READSB_LITE_BEAST_TEST)
#include "beast_tcp.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#ifdef READSB_LITE_AMIPORT
/* Real amiport POSIX shim: not a dummy. stdio remains for text output. */
#define AMIPORT_NO_OPEN_MACROS 1
#define AMIPORT_NO_LOCALE_MACROS 1
#include <amiport/unistd.h>
#else
#include <errno.h>
#endif

#ifdef READSB_AMIGA
/* Avoid the default 4KiB Amiga process stack. */
unsigned long __stack = 131072;
#endif

#define LITE_SAMPLES 32768U
#define LITE_TRAILING 384U
#define LITE_READ_BYTES (LITE_SAMPLES * 2U)

struct _Modes Modes;
void setExit(int arg) { Modes.exitSoon = arg; }
int64_t receiveclock_ms_elapsed(int64_t t1, int64_t t2) { return (t2 - t1) / 12000; }
static struct messageBuffer lite_buffer;
static struct modesMessage lite_message;
static unsigned long lite_decoded;
static uint64_t lite_samples;
static int lite_quiet;

/* QUIET only suppresses message formatting/console I/O. The original
 * demodulation, CRC correction, and message accounting remain unchanged. */
void lite_engine_set_quiet(int enabled) { lite_quiet = !!enabled; }
/* Use the SDR sample clock for the dashboard, the very same timescale
 * used by demodulate2400() to stamp mm->sysTimestamp. Wall elapsed ticks
 * diverge after a blocked console write or temporary scheduling delay. */
uint64_t lite_engine_sample_clock_ms(void) { return lite_samples*1000ULL/2400000ULL; }
uint64_t lite_engine_decoded_total(void) { return (uint64_t)lite_decoded; }
uint64_t lite_engine_preambles_total(void) { return (uint64_t)Modes.stats_current.demod_preambles; }
uint64_t lite_engine_rejected_total(void)
{
    return (uint64_t)Modes.stats_current.demod_rejected_bad +
           (uint64_t)Modes.stats_current.demod_rejected_unknown_icao;
}

struct modesMessage *netGetMM(struct messageBuffer *buffer) {
    (void) buffer;
    memset(&lite_message, 0, sizeof lite_message);
    lite_message.messageBuffer = &lite_buffer;
    return &lite_message;
}

void netUseMessage(struct modesMessage *mm) {
    int bytes, i;
    ++lite_decoded;
    if (lite_interactive_active()) {
        lite_interactive_on_message(mm);
        return; /* No Beast queue and no per-frame console output. */
    }
#if defined(READSB_LITE_AMIPORT) || defined(READSB_LITE_BEAST_TEST)
    /* Beast is queued BEFORE console filtering: Q suppresses text only. */
    beast_tcp_message(mm->msg, (unsigned)mm->msgbits,
                      (uint64_t)mm->timestamp, mm->signalLevel);
#endif
    if (lite_quiet) return;
    bytes = mm->msgbits == MODES_SHORT_MSG_BITS ? MODES_SHORT_MSG_BYTES : MODES_LONG_MSG_BYTES;
    printf("*");
    for (i = 0; i < bytes; ++i) printf("%02X", mm->msg[i]);
    printf("; DF=%d ICAO=%06" PRIX32 " FIX=%d\n", mm->msgtype, (uint32_t)mm->addr & 0xFFFFFFU, mm->correctedbits);
}
void netDrainMessageBuffers(void) {}

/* Reusable full readsb 2.4 Msps decoder. All USB paths use this same code.
 * Keep exact PORT33 demod_2400.c / mode_s.c / CRC / UC8 converter units. */
static uint16_t *lite_mag;
static struct converter_state *lite_state;
static iq_convert_fn lite_convert;
static int lite_active;

int lite_engine_begin(void)
{
    memset(&Modes,0,sizeof(Modes));
    lite_decoded=0;
    lite_samples=0;
    lite_mag=(uint16_t *)calloc(LITE_SAMPLES+LITE_TRAILING+512,
                                sizeof(uint16_t));
    if(!lite_mag) {fprintf(stderr,"[LITE] magnitude allocation failed\n");return -1;}
    Modes.sdr_type=SDR_IFILE;
    Modes.sample_rate=2400000.0;
    Modes.trailing_samples=LITE_TRAILING;
    Modes.preambleThreshold=PREAMBLE_THRESHOLD_DEFAULT;
    Modes.nfix_crc=1;
    Modes.fixDF=1;
    Modes.decodeThreads=1;
    Modes.netMessageBuffer=&lite_buffer;
    memset(&lite_buffer,0,sizeof lite_buffer);
    lite_buffer.msg=&lite_message;
    lite_buffer.alloc=1;
    modesChecksumInit(Modes.nfix_crc);
    icaoFilterInit();
    lite_convert=init_converter(INPUT_UC8,2400000.0,0,&lite_state);
    if(!lite_convert) {
        fprintf(stderr,"[LITE] UC8 converter init failed\n");
        crcCleanupTables();
        icaoFilterDestroy();
        free(lite_mag);lite_mag=NULL;
        return -1;
    }
    lite_active=1;
    return 0;
}

int lite_engine_feed(const unsigned char *iq, size_t nbytes)
{
    struct mag_buf block;
    size_t off=0;
    if (!lite_active || !iq) return -1;
    if (nbytes & 1U) {
        fprintf(stderr,"odd UC8 file length\n");
        return -1;
    }
    while(off<nbytes) {
        size_t n=nbytes-off;
        double level=0.0,power=0.0;
        if(n>LITE_READ_BYTES) n=LITE_READ_BYTES;
        memset(&block,0,sizeof block);
        block.data=lite_mag;
        block.length=(unsigned)(n/2U);
        block.sampleTimestamp=(int64_t)lite_samples*5;
        block.sysTimestamp=(int64_t)(lite_samples*1000/2400000);
        lite_convert((uint8_t *)(iq+off),lite_mag+LITE_TRAILING,
                     block.length,lite_state,&level,&power);
        block.mean_level=level;
        block.mean_power=power;
        demodulate2400(&block);
        memmove(lite_mag,lite_mag+block.length,
                LITE_TRAILING*sizeof(uint16_t));
        lite_samples+=block.length;
        off+=n;
    }
    return 0;
}

void lite_engine_end(int status)
{
    struct mag_buf block;
    if(!lite_active) return;
    /* Preserve PORT33 final overlap demodulation with lookahead padding. */
    memset(lite_mag+LITE_TRAILING,0,
           (LITE_TRAILING+512)*sizeof(uint16_t));
    memset(&block,0,sizeof block);
    block.data=lite_mag;
    block.length=LITE_TRAILING;
    block.sampleTimestamp=(int64_t)lite_samples*5;
    block.sysTimestamp=(int64_t)(lite_samples*1000/2400000);
    demodulate2400(&block);
    if (!lite_interactive_active())
        fprintf(stderr,"[LITE] samples=%" PRIu64 " decoded=%lu status=%d\n",
                lite_samples,lite_decoded,status);
    cleanup_converter(&lite_state);
    /* Essential for RTL burst-loop mode: CRC tables and ICAO cache must
     * not leak into every following capture cycle. */
    crcCleanupTables();
    icaoFilterDestroy();
    free(lite_mag);lite_mag=NULL;
    lite_convert=NULL;
    lite_active=0;
}

#ifdef READSB_LITE_AMIPORT
static int run_file(int fd)
#else
static int run_file(FILE *in)
#endif
{
    unsigned char *iq=(unsigned char *)malloc(LITE_READ_BYTES);
    size_t got;
    int status=0;
    if(!iq) {fprintf(stderr,"out of memory\n");return 1;}
    if(lite_engine_begin()) {free(iq);return 1;}
    for(;;) {
#ifdef READSB_LITE_AMIPORT
        got=0;
        while(got<LITE_READ_BYTES) {
            LONG n=amiport_read(fd,iq+got,(LONG)(LITE_READ_BYTES-got));
            if(n<0) {fprintf(stderr,"amiport_read error\n");status=1;break;}
            if(n==0) break;
            got+=(size_t)n;
        }
        if(status) break;
#else
        got=fread(iq,1,LITE_READ_BYTES,in);
#endif
        if(got & 1) {fprintf(stderr,"odd UC8 file length\n");status=1;break;}
        if(!got) break;
        if(lite_engine_feed(iq,got)) {status=1;break;}
        if(got<LITE_READ_BYTES) {
#ifndef READSB_LITE_AMIPORT
            if(ferror(in)) {perror("fread");status=1;}
#endif
            break;
        }
    }
    lite_engine_end(status);
    free(iq);
    return status;
}

#ifdef READSB_LITE_AMIPORT
static int is_radio_mode(const char *s)
{
    return !strcmp(s,"RFSET") || !strcmp(s,"RTL4") || !strcmp(s,"RTL16") ||
           !strcmp(s,"RTL4Q") || !strcmp(s,"RTL16Q") ||
           !strcmp(s,"RTLPIPE4Q") || !strcmp(s,"RTLPIPE16Q") ||
           !strcmp(s,"RTLSTREAM") || !strcmp(s,"RTLSTREAMQ") ||
           !strcmp(s,"RTLSTREAM60") || !strcmp(s,"RTLSTREAM60Q") ||
           !strcmp(s,"RTLUSB60Q") || !strcmp(s,"RTLINTERACTIVE") ||
           !strcmp(s,"RTLINTERACTIVE60") ||
           !strcmp(s,"RTLSTREAM5") || !strcmp(s,"RTLSTREAM5Q") ||
           !strcmp(s,"RTLBEAST") || !strcmp(s,"RTLBEASTQ") ||
           !strcmp(s,"RTLBEAST60") || !strcmp(s,"RTLBEAST60Q") ||
           !strcmp(s,"RTLBEAST5") || !strcmp(s,"RTLBEAST5Q") ||
           !strcmp(s,"RTLSAVE") || !strcmp(s,"RTL") || !strcmp(s,"RTLLOOP") ||
           !strcmp(s,"RF1090SHORT") || !strcmp(s,"RF1090") ||
           !strcmp(s,"RF1090SAVE");
}
#endif

/* Host compile with READSB_LITE_RF_SIM to exercise the actual in-RAM
 * path (64-KiB RF chunks) against the synthetic regression vectors. */
#ifdef READSB_LITE_RF_SIM
static int run_rf_sim(const char *name)
{
    FILE *fp=fopen(name,"rb");
    unsigned char *buf=NULL;
    size_t got;
    int status=0;
    if(!fp) {perror(name);return 1;}
    buf=(unsigned char *)malloc(LITE_READ_BYTES);
    if(!buf || lite_engine_begin()) {fclose(fp);free(buf);return 1;}
    while((got=fread(buf,1,LITE_READ_BYTES,fp))>0) {
        if(lite_engine_feed(buf,got)) {status=1;break;}
    }
    if(ferror(fp)) status=1;
    lite_engine_end(status);
    free(buf);fclose(fp);
    return status;
}
#endif

int main(int argc, char **argv)
{
#ifdef READSB_LITE_AMIPORT
    int f;
#else
    FILE *f;
#endif
    int result, quiet_file = 0, radio_gain_opt = 0;
    struct lite_gain_option gain = {0,0}; /* tuner AGC by default */
    const char *arg;
    if (argc == 3 && !strcmp(argv[1], "QUIET")) quiet_file = 1;
#ifdef READSB_LITE_AMIPORT
    else if (argc == 3 && is_radio_mode(argv[1])) {
        if (lite_gain_parse(argv[2], &gain)) {
            fprintf(stderr,"[CLI] Bad gain: use GAIN=AUTO or GAIN=0..49.6\n");
            return 2;
        }
        radio_gain_opt=1;
    }
#endif
    else if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.uc8|-> | QUIET <file.uc8|-> | RFSET | RTL4 | RTL16 | RTLPIPE4Q | RTLPIPE16Q | RTLSTREAM[60|5][Q] | RTLUSB60Q | RTLINTERACTIVE[60] | RTLBEAST[60|5][Q] | RTL | RTLSAVE [GAIN=AUTO|GAIN=30|GAIN=40.5]\n", argv[0]);
        return 2;
    }
    arg = radio_gain_opt ? argv[1] : argv[argc-1];
#ifdef READSB_LITE_AMIPORT
    if (!quiet_file && is_radio_mode(arg))
        return lite_rtl_run(arg, gain.manual, gain.tenths_db);
    if (quiet_file && is_radio_mode(arg)) {
        fprintf(stderr, "QUIET <file> is for file input only; use RTLSTREAMQ/RTL16Q for RF\n");
        return 2;
    }
#else
#ifdef READSB_LITE_RF_SIM
    /* Simulates 64 KiB memory feeds, not native USB. */
    lite_engine_set_quiet(quiet_file);
    return run_rf_sim(arg);
#endif
#endif
    lite_engine_set_quiet(quiet_file);
#ifdef READSB_LITE_AMIPORT
    f=strcmp(arg,"-")==0 ? STDIN_FILENO : amiport_open(arg,O_RDONLY);
    if(f<0) {fprintf(stderr,"Cannot open %s\n",arg);return 1;}
#else
    f=strcmp(arg,"-")==0 ? stdin : fopen(arg,"rb");
    if(!f) {perror(arg);return 1;}
#endif
    result=run_file(f);
#ifdef READSB_LITE_AMIPORT
    if(f!=STDIN_FILENO) amiport_close(f);
#else
    if(f!=stdin) fclose(f);
#endif
    return result;
}
