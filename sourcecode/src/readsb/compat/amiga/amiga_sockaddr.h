#ifndef READSB_AMIGA_SOCKADDR_H
#define READSB_AMIGA_SOCKADDR_H
#include <sys/socket.h>
#include <stdint.h>
/* Storage for IPv4/IPv6 socket addresses, never passed by value.
 * Explicitly 128 bytes, at least 8-byte aligned. No fields are accessed
 * except through a struct sockaddr pointer provided to BSD socket APIs.
 */
typedef union readsb_amiga_sockaddr_storage {
    struct sockaddr sa;
    uint64_t align_u64;
    unsigned char bytes[128];
} readsb_sockaddr_storage_t;
#endif
