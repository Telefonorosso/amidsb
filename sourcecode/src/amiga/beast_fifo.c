#include "beast_fifo.h"
#include <string.h>
#include <math.h>

size_t beast_encode(unsigned char *out, size_t cap, const unsigned char *msg,
                    unsigned int bits, uint64_t stamp, double power)
{
    unsigned char plain[21]; /* timestamp[6], signal[1], msg[14] */
    unsigned int bytes, i, n = 0, signal;
    size_t pos = 0;
    if (!out || !msg || cap < BEAST_MAX_PACKET) return 0;
    if (bits != 16U && bits != 56U && bits != 112U) return 0;
    bytes = bits / 8U;
    out[pos++] = 0x1a;
    out[pos++] = bits == 16U ? '1' : bits == 56U ? '2' : '3';
    for (i = 0; i < 6U; ++i)
        plain[n++] = (unsigned char)(stamp >> (8U * (5U-i)));
    if (!(power > 0.0)) signal = 0;
    else if (power >= 1.0) signal = 255;
    else {
        signal = (unsigned int)(sqrt(power) * 255.0 + 0.5);
        if (!signal) signal = 1;
    }
    plain[n++] = (unsigned char)signal;
    memcpy(plain+n, msg, bytes);
    n += bytes;
    for (i = 0; i < n; ++i) {
        out[pos++] = plain[i];
        if (plain[i] == 0x1a) out[pos++] = 0x1a;
    }
    return pos;
}
void beast_fifo_reset(struct beast_fifo *f) { memset(f,0,sizeof(*f)); }
int beast_fifo_push(struct beast_fifo *f, const unsigned char *msg,
                    unsigned int bits, uint64_t stamp, double power)
{
    struct beast_packet *p;
    size_t n;
    if (!f || !msg || (bits != 16U && bits != 56U && bits != 112U)) return -1;
    if (f->count == BEAST_FIFO_CAP) { ++f->overflow; return 0; }
    p = &f->packet[f->tail];
    n = beast_encode(p->data, sizeof p->data, msg, bits, stamp, power);
    if (!n) return -1;
    p->len = (unsigned char)n;
    p->off = 0;
    f->tail = (f->tail + 1U) % BEAST_FIFO_CAP;
    ++f->count;
    ++f->accepted;
    return 1;
}
void beast_fifo_clear_pending(struct beast_fifo *f)
{
    if (!f) return;
    f->disconnected_discarded += f->count;
    f->head = f->tail = f->count = 0;
}
int beast_fifo_drain(struct beast_fifo *f,
                     int (*send_bytes)(void *, const unsigned char *, unsigned int),
                     void *ctx, unsigned int budget)
{
    if (!f || !send_bytes) return -1;
    while (f->count && budget) {
        struct beast_packet *p = &f->packet[f->head];
        unsigned int want = (unsigned int)(p->len-p->off);
        int n;
        if (want > budget) want = budget;
        n = send_bytes(ctx,p->data+p->off,want);
        if (n == -1) return 0; /* EWOULDBLOCK: keep exact byte offset */
        if (n <= 0 || (unsigned)n > want) return -1;
        p->off = (unsigned char)(p->off + n);
        budget -= (unsigned)n;
        if (p->off == p->len) {
            f->head = (f->head + 1U) % BEAST_FIFO_CAP;
            --f->count;
            ++f->sent;
        }
    }
    return 0;
}
