/* PORT38: native task-local bsdsocket.library server, exactly one Beast client.
 * No pthread, epoll, accept() waits, or blocking send().
 * Open/close in SAME task: MiamiDX/Roadshow/AmiTCP socket ABI.
 */
#include "beast_tcp.h"
#include "beast_fifo.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <errno.h>

#ifdef READSB_AMIGA
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <amitcp/socketbasetags.h>
/* MiamiDX BSD errno is NOT necessarily the C runtime errno. */
#define BEAST_WOULDBLOCK 35
struct Library *SocketBase = NULL; /* expected by Amiga SDK inline prototypes */
#else
#include <unistd.h>
#include <fcntl.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#define BEAST_WOULDBLOCK EWOULDBLOCK
#endif

static struct beast_tcp_state {
    struct beast_fifo q;
    int listen_fd, client_fd, running;
    unsigned short port;
    long bsd_errno; /* lives until SocketBase closed */
    uint64_t not_connected, connects, disconnects, rejects, wouldblock;
    uint64_t seen, invalid, heartbeats;
    unsigned int polls;
} s;

static long socket_last_error(void)
{
#ifdef READSB_AMIGA
    return s.bsd_errno;
#else
    return errno;
#endif
}
static void beast_close_fd(int fd)
{
#ifdef READSB_AMIGA
    (void)CloseSocket(fd);
#else
    (void)close(fd);
#endif
}
static int beast_nonblock(int fd)
{
#ifdef READSB_AMIGA
    ULONG one = 1;
    return IoctlSocket(fd,FIONBIO,(char *)&one);
#else
    int old=fcntl(fd,F_GETFL,0);
    return old < 0 ? -1 : fcntl(fd,F_SETFL,old|O_NONBLOCK);
#endif
}
static void beast_disconnect(const char *reason)
{
    if (s.client_fd < 0) return;
    beast_close_fd(s.client_fd);
    s.client_fd=-1;
    ++s.disconnects;
    beast_fifo_clear_pending(&s.q);
    printf("[BEAST] disconnected (%s); listening again\n",reason);
}
int beast_tcp_start(unsigned short port)
{
    struct sockaddr_in a;
    int one=1;
    if (s.running) return -1;
    memset(&s,0,sizeof s);
    s.listen_fd = s.client_fd = -1;
    s.port=port;
    beast_fifo_reset(&s.q);
#ifdef READSB_AMIGA
    SocketBase = OpenLibrary((const unsigned char *)"bsdsocket.library",4);
    if (!SocketBase) { puts("[BEAST] FAIL bsdsocket.library missing"); return -1; }
    if (SocketBaseTags(SBTM_SETVAL(SBTC_ERRNOPTR(sizeof(LONG))),
                       (ULONG)&s.bsd_errno, TAG_DONE)) {
        puts("[BEAST] FAIL configuring BSD errno slot");
        CloseLibrary(SocketBase); SocketBase=NULL;
        return -1;
    }
#endif
    s.listen_fd=socket(AF_INET,SOCK_STREAM,0);
    if (s.listen_fd < 0) goto fail;
    (void)setsockopt(s.listen_fd,SOL_SOCKET,SO_REUSEADDR,(const char *)&one,sizeof one);
    if (beast_nonblock(s.listen_fd)) goto fail;
    memset(&a,0,sizeof a);
    a.sin_family=AF_INET;
    /* Write the port in network byte order without relying on a target
     * libc htons() symbol. INADDR_ANY is all zeros on both endiannesses. */
    ((unsigned char *)&a.sin_port)[0]=(unsigned char)(port >> 8);
    ((unsigned char *)&a.sin_port)[1]=(unsigned char)port;
    a.sin_addr.s_addr=0;
    if (bind(s.listen_fd,(struct sockaddr *)&a,sizeof a) < 0) goto fail;
    if (listen(s.listen_fd,1) < 0) goto fail;
    s.running=1;
    printf("[BEAST] listening TCP 0.0.0.0:%u (one client, no auth; LAN only)\n",(unsigned)port);
    return 0;
fail:
    printf("[BEAST] FAIL socket/bind/listen BSD_errno=%ld\n",socket_last_error());
    beast_tcp_stop();
    return -1;
}
void beast_tcp_message(const unsigned char *msg, unsigned int msgbits,
                       uint64_t timestamp, double power)
{
    if (!s.running) return;
    ++s.seen;
    if (s.client_fd < 0) { ++s.not_connected; return; }
    if (beast_fifo_push(&s.q,msg,msgbits,timestamp,power) < 0) ++s.invalid;
}
static int beast_send_callback(void *unused,const unsigned char *data,unsigned int bytes)
{
    int n;
    long e;
    (void)unused;
    n=send(s.client_fd,(const char *)data,(int)bytes,
#ifdef READSB_AMIGA
           0
#else
           MSG_NOSIGNAL
#endif
    );
    if (n >= 0) return n;
    e=socket_last_error();
    if (e == BEAST_WOULDBLOCK
#ifndef READSB_AMIGA
        || e == EAGAIN || e == EINTR
#endif
       ) { ++s.wouldblock; return -1; }
    return -2;
}
void beast_tcp_poll(void)
{
    int fd;
    if (!s.running) return;
    /* Called after every 4 received 64KiB blocks (~55ms at 2.4Msps).
     * Bounded work and nonblocking sockets protect the 1090MHz DSP.
     */
    if (++s.polls % 4U) return;
    fd=accept(s.listen_fd,NULL,NULL);
    if (fd >= 0) {
        if (s.client_fd >= 0) {
            ++s.rejects;
            beast_close_fd(fd);
        } else if (beast_nonblock(fd)) {
            beast_close_fd(fd);
            puts("[BEAST] reject client: nonblocking setup failed");
        } else {
            s.client_fd=fd;
            beast_fifo_clear_pending(&s.q);
            ++s.connects;
            puts("[BEAST] client connected");
        }
    }
    if (s.client_fd < 0) return;
    /* A silent/slow client never blocks. At most 2048 bytes per poll. */
    if (beast_fifo_drain(&s.q,beast_send_callback,NULL,2048U))
        beast_disconnect("send failed/EOF");
}
void beast_tcp_heartbeat(void)
{
    const unsigned char empty[2] = {0,0};
    if (s.running && s.client_fd >= 0) {
        if (beast_fifo_push(&s.q,empty,16U,0,0.0)==1) ++s.heartbeats;
    }
}
void beast_tcp_report(void)
{
    if (!s.running) return;
    printf("[BEAST] client=%s connects=%" PRIu64 " disconnects=%" PRIu64 " queued=%u sent=%" PRIu64 " dropped=%" PRIu64 " no_client=%" PRIu64 " wouldblock=%" PRIu64 " rejects=%" PRIu64 " heartbeats=%" PRIu64 " discarded=%" PRIu64 " invalid=%" PRIu64 "\n",
           s.client_fd >= 0 ? "yes" : "no",
           s.connects,s.disconnects,
           s.q.count,s.q.sent,s.q.overflow,s.not_connected,
           s.wouldblock,s.rejects,s.heartbeats,
           s.q.disconnected_discarded,s.invalid);
}
void beast_tcp_stop(void)
{
    if (s.client_fd>=0) { beast_close_fd(s.client_fd); s.client_fd=-1; }
    if (s.listen_fd>=0) { beast_close_fd(s.listen_fd); s.listen_fd=-1; }
#ifdef READSB_AMIGA
    if (SocketBase) { CloseLibrary(SocketBase); SocketBase=NULL; }
#endif
    s.running=0;
}

#ifndef READSB_AMIGA
unsigned beast_tcp_bound_port(void)
{
    struct sockaddr_in a;
    socklen_t n=sizeof a;
    memset(&a,0,sizeof a);
    if (s.listen_fd<0 || getsockname(s.listen_fd,(struct sockaddr *)&a,&n)<0) return 0;
    return (unsigned)ntohs(a.sin_port);
}

#endif /* !READSB_AMIGA: host test only */
