#include "rx_stream.h"
#include <string.h>

int lite_stream_run(const struct lite_stream_ops *ops, void *opaque,
                    unsigned char *b0, unsigned char *b1,
                    unsigned long chunk_bytes, uint64_t max_chunks,
                    struct lite_stream_stats *stats)
{
    unsigned char *buffers[2];
    unsigned long actual;
    int slot = 0, pending = 0, ending, rc;
    if (!ops || !stats || !ops->send || !ops->wait || !ops->abort ||
        !ops->consume || !b0 || !b1 || b0 == b1 ||
        !chunk_bytes || (chunk_bytes & 1UL) ||
        (!max_chunks && !ops->stop)) return -1;
    memset(stats, 0, sizeof(*stats));
    buffers[0] = b0;
    buffers[1] = b1;
    if (ops->send(opaque, buffers[slot], chunk_bytes)) return -1;
    pending = 1;
    ++stats->submitted;
    for (;;) {
        actual = 0;
        rc = ops->wait(opaque, &actual);
        pending = 0; /* wait() reaps, even on failure */
        if (rc || actual != chunk_bytes) return -1;
        ++stats->completed;
        stats->bytes += actual;
        ending = ((max_chunks && stats->completed >= max_chunks) ||
                  (ops->stop && ops->stop(opaque, stats)));
        /* While the current block is demodulated, the alternate buffer is
         * filled. On stop we do NOT queue a new USB request. */
        if (!ending) {
            if (ops->send(opaque, buffers[slot ^ 1], chunk_bytes)) return -1;
            pending = 1;
            ++stats->submitted;
        }
        if (ops->consume(opaque, buffers[slot], chunk_bytes)) {
            if (pending) {
                ops->abort(opaque); /* must also reap the completion */
                ++stats->aborted;
                pending = 0;
            }
            return -1;
        }
        ++stats->delivered;
        if (ops->report) ops->report(opaque, stats);
        if (ending) break;
        slot ^= 1;
    }
    return 0;
}
