/* Namespace for required POSIX clock IDs. Definitions only, NO clock implementation.
 * These numeric values are internal until an Amiga clock_gettime backend is linked.
 * Real monotonic and per-thread CPU clocks are required for functional equivalence.
 */
#ifndef READSB_AMIGA_CLOCK_IDS_H
#define READSB_AMIGA_CLOCK_IDS_H
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#ifndef CLOCK_THREAD_CPUTIME_ID
#define CLOCK_THREAD_CPUTIME_ID 3
#endif
#endif
