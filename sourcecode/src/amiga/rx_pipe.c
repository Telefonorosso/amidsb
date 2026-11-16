#include "rx_pipe.h"
#include <string.h>

int lite_pipe_run(const struct lite_pipe_ops *ops, void *opaque,
                  unsigned char *b0, unsigned char *b1,
                  unsigned long chunk_bytes, unsigned long chunks,
                  struct lite_pipe_stats *stats)
{
    unsigned char *buffers[2];
    unsigned long i, actual = 0;
    int slot = 0, pending = 0;
    if (!stats || !ops || !ops->send || !ops->wait || !ops->abort ||
        !ops->consume || !b0 || !b1 || b0 == b1 || !chunk_bytes ||
        (chunk_bytes & 1UL) || !chunks) return -1;
    memset(stats, 0, sizeof(*stats));
    buffers[0] = b0;
    buffers[1] = b1;
    if (ops->send(opaque, buffers[slot], chunk_bytes)) return -1;
    pending = 1;
    ++stats->submitted;
    for (i = 0; i < chunks; ++i) {
        actual = 0;
        /* Waiting concludes the previous request, including error cases. */
        {
            int rc = ops->wait(opaque, &actual);
            pending = 0;
            if (rc || actual != chunk_bytes) return -1;
        }
        ++stats->completed;
        stats->bytes += actual;
        /* Launch next USB request BEFORE demodulating the completed buffer.
         * The new request uses the OTHER buffer and cannot overwrite it. */
        if (i+1 < chunks) {
            if (ops->send(opaque, buffers[slot ^ 1], chunk_bytes))
                return -1;
            pending = 1;
            ++stats->submitted;
        }
        if (ops->consume(opaque, buffers[slot], chunk_bytes)) {
            if (pending) {
                ops->abort(opaque);
                ++stats->aborted;
                pending = 0;
            }
            return -1;
        }
        ++stats->delivered;
        slot ^= 1;
    }
    return 0;
}
