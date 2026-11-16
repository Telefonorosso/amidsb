/* PORT39 interactive tracker API (single-task, no networking, no ncurses). GPL-3.0-or-later. */
#ifndef LITE_INTERACTIVE_H
#define LITE_INTERACTIVE_H
#include <stdint.h>
struct modesMessage;
struct lite_int_snapshot {
    uint32_t addr;
    unsigned messages;
    uint64_t seen_ms;
    int alt_ft, speed_kts, heading_deg;
    unsigned squawk;
    double lat, lon, rssi_db;
    char callsign[16];
    int ground;
    unsigned valid; /* mask: 1 altitude, 2 ground speed, 4 track, 8 squawk, 16 position, 32 callsign */
};
int lite_interactive_begin(int draw_screen);
void lite_interactive_on_message(const struct modesMessage *mm);
void lite_interactive_render(uint64_t now_ms, unsigned long kbps);
void lite_interactive_end(void);
int lite_interactive_active(void);
void lite_interactive_discard_input(void);
int lite_interactive_snapshot(uint32_t addr, struct lite_int_snapshot *out);
unsigned lite_interactive_count(uint64_t now_ms);
#endif
