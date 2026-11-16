/* readsb-Amiga portability overlay — GPL-3.0-or-later.
 * Minimal host/little- and target/big-endian conversions; no runtime shims.
 * Used only from port/compat/compat.h under READSB_AMIGA.
 */
#ifndef READSB_AMIGA_ENDIAN_H
#define READSB_AMIGA_ENDIAN_H
#include <stdint.h>
#if !defined(__BYTE_ORDER__) || !defined(__ORDER_BIG_ENDIAN__) || !defined(__ORDER_LITTLE_ENDIAN__)
#error GCC byte order macros required
#endif
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__ && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Unhandled target byte order
#endif
#ifndef bswap_16
#define bswap_16(x) __builtin_bswap16((uint16_t)(x))
#endif
#ifndef bswap_32
#define bswap_32(x) __builtin_bswap32((uint32_t)(x))
#endif
#ifndef bswap_64
#define bswap_64(x) __builtin_bswap64((uint64_t)(x))
#endif
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#ifndef le16toh
#define le16toh(x) bswap_16(x)
#endif
#ifndef le32toh
#define le32toh(x) bswap_32(x)
#endif
#ifndef le64toh
#define le64toh(x) bswap_64(x)
#endif
#ifndef htole16
#define htole16(x) bswap_16(x)
#endif
#ifndef htole32
#define htole32(x) bswap_32(x)
#endif
#ifndef htole64
#define htole64(x) bswap_64(x)
#endif
#ifndef be16toh
#define be16toh(x) ((uint16_t)(x))
#endif
#ifndef be32toh
#define be32toh(x) ((uint32_t)(x))
#endif
#ifndef be64toh
#define be64toh(x) ((uint64_t)(x))
#endif
#ifndef htobe16
#define htobe16(x) ((uint16_t)(x))
#endif
#ifndef htobe32
#define htobe32(x) ((uint32_t)(x))
#endif
#ifndef htobe64
#define htobe64(x) ((uint64_t)(x))
#endif
#else
#ifndef le16toh
#define le16toh(x) ((uint16_t)(x))
#endif
#ifndef le32toh
#define le32toh(x) ((uint32_t)(x))
#endif
#ifndef le64toh
#define le64toh(x) ((uint64_t)(x))
#endif
#ifndef htole16
#define htole16(x) ((uint16_t)(x))
#endif
#ifndef htole32
#define htole32(x) ((uint32_t)(x))
#endif
#ifndef htole64
#define htole64(x) ((uint64_t)(x))
#endif
#ifndef be16toh
#define be16toh(x) bswap_16(x)
#endif
#ifndef be32toh
#define be32toh(x) bswap_32(x)
#endif
#ifndef be64toh
#define be64toh(x) bswap_64(x)
#endif
#ifndef htobe16
#define htobe16(x) bswap_16(x)
#endif
#ifndef htobe32
#define htobe32(x) bswap_32(x)
#endif
#ifndef htobe64
#define htobe64(x) bswap_64(x)
#endif
#endif
#endif
