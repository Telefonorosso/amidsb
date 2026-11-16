#ifndef READSB_AMIGA_SOCKET_H
#define READSB_AMIGA_SOCKET_H
#include <sys/types.h>
#include <sys/socket.h>
/* A readsb-only request flag. It is consumed by readsb_amiga_socket_create
 * before calling socket() and is NEVER passed to the target BSD stack.
 * On systems with native SOCK_NONBLOCK use its native definition only as
 * a request selector, not as a socket type bit. */
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 0x40000000
#endif
#ifdef __cplusplus
extern "C" {
#endif
int readsb_amiga_socket_set_nonblock(int fd);
int readsb_amiga_socket_close(int fd);
int readsb_amiga_socket_create(int domain, int protocol, int flags);
int readsb_amiga_socket_accept4(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags);
#ifdef __cplusplus
}
#endif
#endif
