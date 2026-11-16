#ifndef READSB_AMIGA_EVENT_ABI_H
#define READSB_AMIGA_EVENT_ABI_H
/* readsb/Linux epoll/eventfd surface, for compilation inventory only.
 * NO FUNCTIONS IMPLEMENTED HERE. Do not link until a real AmigaOS backend
 * implements descriptor lifetime, registration, wakeups, timeouts and errors.
 * Constants mirror Linux epoll, NOT automatically Amiga bsdsocket flags. */
#include <stdint.h>
#include <fcntl.h>
#define EPOLLIN 0x0001u
#define EPOLLPRI 0x0002u
#define EPOLLOUT 0x0004u
#define EPOLLERR 0x0008u
#define EPOLLHUP 0x0010u
#define EPOLLRDHUP 0x2000u
#define EPOLLEXCLUSIVE (1u << 28)
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3
#define EFD_NONBLOCK O_NONBLOCK
#ifdef __cplusplus
extern "C" {
#endif
typedef union epoll_data { void *ptr; int fd; uint32_t u32; uint64_t u64; } epoll_data_t;
struct epoll_event { uint32_t events; epoll_data_t data; };
int epoll_create(int size);
int epoll_create1(int flags);
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
int eventfd(unsigned int initial_value, int flags);
#ifdef __cplusplus
}
#endif
#endif
