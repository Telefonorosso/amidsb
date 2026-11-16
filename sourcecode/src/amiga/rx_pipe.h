#ifndef READSB_LITE_RX_PIPE_H
#define READSB_LITE_RX_PIPE_H
/* Native/host shared ping-pong driver. One USB transfer outstanding at a time;
 * next transfer can run while DSP consumes the previously completed buffer.
 * No POSIX threading or assumption of concurrent decoding callbacks.
 */
struct lite_pipe_stats {
    unsigned long submitted;
    unsigned long completed;
    unsigned long delivered;
    unsigned long bytes;
    unsigned long aborted;
};
struct lite_pipe_ops {
    int (*send)(void *opaque, unsigned char *dst, unsigned long nbytes);
    int (*wait)(void *opaque, unsigned long *actual);
    void (*abort)(void *opaque);
    int (*consume)(void *opaque, const unsigned char *buf, unsigned long nbytes);
};
/* Success=0; failure=-1. On return no transfers remain pending.
 * Send() must leave a pending transfer if it returns 0. Wait() must consume
 * the pending transfer even when reporting an error. Abort() must abort and
 * reap the pending transfer, so buffers are safe to free afterwards.
 */
int lite_pipe_run(const struct lite_pipe_ops *ops, void *opaque,
                  unsigned char *b0, unsigned char *b1,
                  unsigned long chunk_bytes, unsigned long chunks,
                  struct lite_pipe_stats *stats);
#endif
