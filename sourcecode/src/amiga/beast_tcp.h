#ifndef READSB_LITE_BEAST_TCP_H
#define READSB_LITE_BEAST_TCP_H
#include <stdint.h>
/* One-client, nonblocking Beast TCP: Amiga bsdsocket.library (MiamiDX).
 * No network work is performed from Poseidon callbacks. Must be called
 * only from the same Exec task that called beast_tcp_start().
 */
int beast_tcp_start(unsigned short port);
void beast_tcp_message(const unsigned char *msg, unsigned int msgbits,
                       uint64_t timestamp, double signal_power);
void beast_tcp_poll(void); /* call after decoded RF block, time-budgeted */
void beast_tcp_report(void);
void beast_tcp_heartbeat(void); /* enqueue readsb-compatible idle heartbeat */
unsigned beast_tcp_bound_port(void); /* host test uses ephemeral port */
void beast_tcp_stop(void);
#endif
