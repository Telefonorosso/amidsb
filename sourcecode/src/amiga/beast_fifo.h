#ifndef READSB_LITE_BEAST_FIFO_H
#define READSB_LITE_BEAST_FIFO_H
/* Bounded Beast binary messages; pure ISO C, usable in Linux host tests.
 * GPL-3.0-or-later, compatible with upstream readsb.
 */
#include <stdint.h>
#include <stddef.h>
#define BEAST_FIFO_CAP 256U
#define BEAST_MAX_PACKET 44U /* delimiter+type + at most (6+1+14)*2 */
struct beast_packet {
    unsigned char data[BEAST_MAX_PACKET];
    unsigned char len;
    unsigned char off;
};
struct beast_fifo {
    struct beast_packet packet[BEAST_FIFO_CAP];
    unsigned int head, tail, count;
    uint64_t accepted, sent, overflow, disconnected_discarded;
};
/* Frame types: 16-bit Mode A/C = '1', 56-bit Mode S = '2', 112-bit = '3'.
 * Timestamp is readsb's 12-MHz receiver sample clock (48 bits transmitted).
 * Signal is linear power (0..1), encoded as sqrt(power)*255.
 * Returns encoded byte length, or zero for unsupported message length/args.
 */
size_t beast_encode(unsigned char *dst, size_t cap, const unsigned char *msg,
                    unsigned int msgbits, uint64_t timestamp, double signal_power);
void beast_fifo_reset(struct beast_fifo *f);
/* Returns 1 on queued, 0 on full, -1 on invalid frame. */
int beast_fifo_push(struct beast_fifo *f, const unsigned char *msg,
                    unsigned int msgbits, uint64_t timestamp, double signal_power);
/* Sends at most budget bytes, via callback: >0 bytes sent, 0 peer disconnected,
 * -1 would-block, -2 fatal. Returns 0 healthy, -1 disconnected/fatal.
 * The callback MUST be nonblocking and MUST NOT be a POSIX write on Amiga.
 */
int beast_fifo_drain(struct beast_fifo *f,
                     int (*send_bytes)(void *, const unsigned char *, unsigned int),
                     void *ctx, unsigned int budget);
void beast_fifo_clear_pending(struct beast_fifo *f);
#endif
