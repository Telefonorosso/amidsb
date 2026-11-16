/* readsb AmigaOS 68k anonymous mapping replacement.
 * GPL-3.0-or-later. Used only for private, read/write anonymous allocations.
 * Original upstream algorithms remain unchanged.
 */
#ifndef READSB_AMIGA_ANONMEM_H
#define READSB_AMIGA_ANONMEM_H
#include <stddef.h>
void *readsb_amiga_anon_alloc(size_t size);
int readsb_amiga_anon_free(void *ptr, size_t size);
#endif
