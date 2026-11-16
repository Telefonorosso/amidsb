#ifndef READSB_AMIGA_POSIX_PROTOTYPES_H
#define READSB_AMIGA_POSIX_PROTOTYPES_H
/* PORT7: required function declarations absent from this toolchain's headers.
 * These are NOT implementations. Each remains a real unresolved symbol until
 * backed by AmigaOS runtime code. Avoid changing prototypes on POSIX hosts. */
#if defined(READSB_AMIGA) && !defined(__linux__) && !defined(__APPLE__)
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <netdb.h>
#ifdef __cplusplus
extern "C" {
#endif
/* clock IDs are defined separately in amiga_posix_clock_ids.h */
/* Some target runtimes expose nanosleep as a function-like macro.
 * A C function prototype for a macro expands to invalid tokens. Respect the
 * runtime declaration rather than redefining or #undef-ing its API. */
#ifndef nanosleep
int nanosleep(const struct timespec *request, struct timespec *remaining);
#endif
#ifndef clock_nanosleep
int clock_nanosleep(clockid_t clock_id, int flags,
                    const struct timespec *request, struct timespec *remaining);
#endif
/* Do NOT redeclare clock_gettime: target <time.h> declares it with its
 * own clockid_t ABI. PORT7's int prototype conflicted with that type.
 * The actual implementation/linkability is checked separately. */
int pthread_tryjoin_np(pthread_t thread, void **value_ptr);
int getpriority(int which, int who);
int setpriority(int which, int who, int value);
/* DNS compatibility lives in amiga_dns_contract.h.  Its explicit
 * readsb_amiga_* symbols remain UNRESOLVED until a real BSDsocket adapter
 * is implemented; they must never return fabricated success. */
int sched_get_priority_max(int policy);
int sched_get_priority_min(int policy);
#ifdef __cplusplus
}
#endif
#endif
#endif
