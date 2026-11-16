/* readsb AmigaOS 68k portability patch, GPL-3.0-or-later.
 * Workaround hardware tested with all UC8 input pairs, Nov 2026.
 * Keep original readsb source unmodified: inject into WORK copy only.
 */
#ifndef READSB_AMIGA_MAG_U16_H
#define READSB_AMIGA_MAG_U16_H
#include <stdint.h>
#include <math.h>
static inline uint16_t amiga_readsb_mag_u16(float magnitude)
{
    float v;
    if (!(magnitude > 0.0f)) return (uint16_t)0;
    if (magnitude >= 1.0f) return UINT16_MAX;
    v = magnitude * 65535.0f + 0.5f;
    if (v >= 65535.0f) return UINT16_MAX;
    return (uint16_t)(int32_t)v;
}
#endif
