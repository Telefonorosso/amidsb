#ifndef READSB_AMIGA_CPU_H
#define READSB_AMIGA_CPU_H
/* AmigaOS 68k exposes one executable CPU to native tasks, including when
 * Emu68 runs on a multicore ARM board. This adapts CPU topology/affinity,
 * NOT the readsb worker thread count or its scheduling implementation.
 */
#include <errno.h>
static inline int readsb_amiga_logical_cpu_count(void) { return 1; }
/* Return error *number*, as upstream threadAffinity does (not errno/-1). */
static inline int readsb_amiga_thread_affinity(int cpu_id) {
    return cpu_id == 0 ? 0 : EINVAL;
}
#endif
