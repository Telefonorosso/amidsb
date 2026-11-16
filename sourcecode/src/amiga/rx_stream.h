#ifndef READSB_LITE_RX_STREAM_H
#define READSB_LITE_RX_STREAM_H
/* PORT37 continuous ping-pong stream. A single USB request may be outstanding.
 * No forced end-of-burst gap; no work buffers allocated inside the loop.
 * Counters use uint64_t so long-running streams cannot roll at 4 GiB.
 */
#include <stdint.h>
struct lite_stream_stats {
    uint64_t submitted, completed, delivered, bytes, aborted;
};
struct lite_stream_ops {
    int (*send)(void *opaque, unsigned char *dst, unsigned long nbytes);
    int (*wait)(void *opaque, unsigned long *actual);
    void (*abort)(void *opaque);
    int (*consume)(void *opaque, const unsigned char *buf, unsigned long nbytes);
    /* Called after receiving a block, before submitting the next request.
     * Return nonzero to finish normally after consuming the current block. */
    int (*stop)(void *opaque, const struct lite_stream_stats *stats);
    /* Called after each delivered block. Must not modify the USB buffers. */
    void (*report)(void *opaque, const struct lite_stream_stats *stats);
};
/* max_chunks=0 means run until stop() requests shutdown or an error occurs.
 * On return no USB transfer remains outstanding, including on feed failure.
 * wait() must consume/reap its completion even on error. */
int lite_stream_run(const struct lite_stream_ops *ops, void *opaque,
                    unsigned char *b0, unsigned char *b1,
                    unsigned long chunk_bytes, uint64_t max_chunks,
                    struct lite_stream_stats *stats);
#endif
