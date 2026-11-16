/* PORT39 native Amiga interactive console. No ncurses, sockets or pthreads.
 * Native console.device accepts ESC [ CSI and 0x9B. Keeps original readsb.cpr.c.
 * Single task: netUseMessage() updates, stream report() refreshes at 1Hz.
 * GPL-3.0-or-later, derived field layout from readsb upstream interactive.c.
 */
#include "readsb.h"
#include "lite_interactive.h"
#include "cpr.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#ifdef READSB_AMIGA
#include <dos/dos.h>
#include <proto/dos.h>
/* Only interactive mode switches the existing CON: input to RAW. */
static BPTR interactive_input;
static int interactive_raw_active;
#endif

#define INT_CAP 128
#define INT_ROWS 20
#define INT_TTL_MS 60000ULL
#define CPR_PAIR_MS 10000ULL
#define CPR_POS_MS 15000ULL
#define VALID_ALT 1U
#define VALID_GS 2U
#define VALID_TRACK 4U
#define VALID_SQUAWK 8U
#define VALID_POS 16U
#define VALID_CALL 32U

struct air_entry {
    struct lite_int_snapshot d;
    uint64_t cpr_ms[2], pos_ms;
    unsigned even_lat, even_lon, odd_lat, odd_lon;
    unsigned cpr_valid[2];
    double sig[8];
    unsigned sig_pos;
};
static struct air_entry aircraft[INT_CAP];
static int enabled, screen;
static uint64_t last_draw_ms;
static char old_rows[INT_ROWS + 2][140];

static struct air_entry *lookup(uint32_t addr, uint64_t now) {
    struct air_entry *unused = 0, *oldest = 0;
    unsigned i;
    for (i=0;i<INT_CAP;++i) {
        struct air_entry *a=&aircraft[i];
        if (a->d.addr == addr) return a;
        if (!a->d.addr && !unused) unused=a;
        if (!oldest || a->d.seen_ms < oldest->d.seen_ms) oldest=a;
    }
    if (!unused) {
        if (oldest && now >= oldest->d.seen_ms && now-oldest->d.seen_ms>INT_TTL_MS) unused=oldest;
        else return NULL; /* table full of active aircraft; keep existing tracks */
    }
    memset(unused,0,sizeof(*unused));
    unused->d.addr=addr;
    return unused;
}
static double sig_db(const struct air_entry *a) {
    double sum=0;
    unsigned i;
    for (i=0;i<8;++i) sum+=a->sig[i];
    sum/=8.0;
    if (sum<1e-9) sum=1e-9;
    return 10.0*log10(sum);
}
static void process_cpr(struct air_entry *a, const struct modesMessage *mm, uint64_t now) {
    int odd=!!mm->cpr_odd;
    double lat=0,lon=0;
    uint64_t age;
    if (!mm->cpr_valid || mm->cpr_type != CPR_AIRBORNE) return;
    a->cpr_ms[odd]=now;
    a->cpr_valid[odd]=1;
    if (odd) {a->odd_lat=mm->cpr_lat; a->odd_lon=mm->cpr_lon;}
    else {a->even_lat=mm->cpr_lat; a->even_lon=mm->cpr_lon;}
    if (!a->cpr_valid[0] || !a->cpr_valid[1]) return;
    age = a->cpr_ms[0] > a->cpr_ms[1] ?
           a->cpr_ms[0]-a->cpr_ms[1] : a->cpr_ms[1]-a->cpr_ms[0];
    if (age > CPR_PAIR_MS) return;
    if (decodeCPRairborne(a->even_lat,a->even_lon,a->odd_lat,a->odd_lon,
                          odd,&lat,&lon) != 0) return;
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return;
    /* Coarse plausibility check: ~1200 kt plus 2 km positioning allowance.
     * Reject implausible global CPR jumps, not ordinary turns/speed changes. */
    if ((a->d.valid & VALID_POS) && now > a->pos_ms) {
        double dt=(double)(now-a->pos_ms)/1000.0;
        double dy=(lat-a->d.lat)*111200.0;
        double mid=(lat+a->d.lat)*0.008726646259971648; /* radians midpoint */
        double dx=(lon-a->d.lon)*111200.0*cos(mid);
        double d=sqrt(dx*dx+dy*dy);
        if (dt<120.0 && d>2000.0+620.0*dt) return;
    }
    a->d.lat=lat; a->d.lon=lon; a->pos_ms=now;
    a->d.valid |= VALID_POS;
}
int lite_interactive_begin(int draw_screen) {
#ifdef READSB_AMIGA
    /* CON: cooked line editing holds all pending output when any key is typed.
     * SetMode(Input(),1) disables line editing/echo, without opening a new
     * window or allocating an extra task. Ctrl-C remains a DOS break signal.
     * On exit MUST restore cooked mode so Shell input works normally. */
    interactive_raw_active=0;
    interactive_input=0;
    if (draw_screen) {
        BPTR in=Input();
        if (!in || !SetMode(in,1)) return 0;
        interactive_input=in;
        interactive_raw_active=1;
    }
#endif
    memset(aircraft,0,sizeof(aircraft));
    memset(old_rows,0,sizeof(old_rows));
    last_draw_ms=0;
    screen=!!draw_screen;
    enabled=1;
    if (screen) {
        /* AmigaOS console.device recognizes ESC[ CSI; no ncurses required. */
        fputs("\x1b[1;1H\x1b[J",stdout);
        fflush(stdout);
    }
    return 1;
}
/* Consume keyboard type-ahead without blocking and without echoing. Doing
 * this periodically prevents the CON: raw input queue from filling while
 * the receiver runs continuously. Break keys remain Exec signals. */
void lite_interactive_discard_input(void) {
#ifdef READSB_AMIGA
    unsigned i;
    char c;
    if (!interactive_raw_active) return;
    for (i=0; i<32U && WaitForChar(interactive_input,0); ++i) {
        if (Read(interactive_input,&c,1)!=1) break;
    }
#endif
}
int lite_interactive_active(void) { return enabled; }
void lite_interactive_on_message(const struct modesMessage *mm) {
    struct air_entry *a;
    uint64_t now;
    uint32_t addr;
    if (!enabled || !mm) return;
    addr=mm->addr & 0xFFFFFFU;
    if (!addr) return;
    now=mm->sysTimestamp>=0?(uint64_t)mm->sysTimestamp:0;
    a=lookup(addr,now);
    if (!a) return;
    a->d.seen_ms=now;
    if (a->d.messages<99999U) ++a->d.messages;
    if (mm->callsign_valid) {
        memcpy(a->d.callsign,mm->callsign,sizeof(a->d.callsign)-1);
        a->d.callsign[sizeof(a->d.callsign)-1]=0;
        a->d.valid |= VALID_CALL;
    }
    if (mm->airground==AG_GROUND) a->d.ground=1;
    else if (mm->airground==AG_AIRBORNE) a->d.ground=0;
    if (mm->baro_alt_valid) {
        a->d.alt_ft = (mm->baro_alt_unit==UNIT_METERS) ?
                      (int)(mm->baro_alt*3.28084) : mm->baro_alt;
        a->d.valid |= VALID_ALT;
    }
    if (mm->gs_valid) {
        a->d.speed_kts=(int)(mm->gs.v0+0.5f);
        a->d.valid |= VALID_GS;
    }
    if (mm->track_valid) {
        a->d.heading_deg=(int)(mm->heading+0.5f) % 360;
        a->d.valid |= VALID_TRACK;
    }
    if (mm->squawk_valid) {
        a->d.squawk = mm->squawkHex & 0xFFFFU;
        a->d.valid |= VALID_SQUAWK;
    }
    if (mm->signalLevel>0.0 && mm->signalLevel<=1.0) {
        a->sig[a->sig_pos++ & 7U] = mm->signalLevel;
    }
    a->d.rssi_db=sig_db(a);
    process_cpr(a,mm,now);
}
unsigned lite_interactive_count(uint64_t now_ms) {
    unsigned i,n=0;
    for(i=0;i<INT_CAP;++i)
        if (aircraft[i].d.addr && now_ms>=aircraft[i].d.seen_ms &&
            now_ms-aircraft[i].d.seen_ms<INT_TTL_MS && aircraft[i].d.messages>1) ++n;
    return n;
}
int lite_interactive_snapshot(uint32_t addr, struct lite_int_snapshot *out) {
    unsigned i;
    if (!enabled || !out) return 0;
    for (i=0;i<INT_CAP;++i) if(aircraft[i].d.addr==addr) {
        *out=aircraft[i].d;
        return 1;
    }
    return 0;
}
static int cmp_alt(const void *aa,const void *bb) {
    const struct air_entry *a=*(const struct air_entry *const *)aa;
    const struct air_entry *b=*(const struct air_entry *const *)bb;
    int av=!!(a->d.valid&VALID_ALT), bv=!!(b->d.valid&VALID_ALT);
    if (av!=bv) return bv-av;
    if (av && a->d.alt_ft!=b->d.alt_ft) return a->d.alt_ft<b->d.alt_ft?-1:1;
    return a->d.addr<b->d.addr?-1:(a->d.addr>b->d.addr?1:0);
}
static void paint(unsigned row, const char *line) {
    char pos[24];
    size_t n;
    if (row >= INT_ROWS+2U || !screen) return;
    if (!strcmp(old_rows[row],line)) return;
    snprintf(old_rows[row],sizeof(old_rows[row]),"%s",line);
    n=(size_t)snprintf(pos,sizeof(pos),"\x1b[%u;1H",row+1U);
    fwrite(pos,1,n,stdout);
    fputs("\x1b[K",stdout); /* clear at column 1, before possibly filling 80 columns */
    fputs(line,stdout);
}
void lite_interactive_render(uint64_t now_ms, unsigned long kbps) {
    struct air_entry *active[INT_CAP];
    unsigned n=0, i, row;
    char line[140];
    (void)kbps; /* No diagnostics in the upstream-like screen. */
    if (!enabled || !screen) return;
    if (last_draw_ms && now_ms>=last_draw_ms && now_ms-last_draw_ms<1000ULL) return;
    last_draw_ms=now_ms;
    snprintf(line,sizeof(line),"%cHex    Mode  Sqwk  Flight     Alt   Spd  Hdg      Lat     Long  RSSI  Msgs Seen",
             "|/-\\"[(now_ms/1000ULL)%4U]);
    paint(0,line);
    paint(1,"-----------------------------------------------------------------------------------");
    for (i=0;i<INT_CAP;++i) {
        struct air_entry *a=&aircraft[i];
        if (a->d.addr && a->d.messages>1 && now_ms>=a->d.seen_ms &&
            now_ms-a->d.seen_ms<INT_TTL_MS) active[n++]=a;
    }
    qsort(active,n,sizeof(active[0]),cmp_alt);
    for (row=0;row<INT_ROWS;++row) {
        if (row>=n) {paint(row+2U,"");continue;}
        {
            const struct lite_int_snapshot *d=&active[row]->d;
            char sq[8]="", alt[9]="",spd[8]="",hdg[8]="",lat[16]="",lon[16]="";
            if(d->valid&VALID_SQUAWK) snprintf(sq,sizeof(sq),"%04x",d->squawk);
            if(d->ground) snprintf(alt,sizeof(alt),"grnd");
            else if(d->valid&VALID_ALT) snprintf(alt,sizeof(alt),"%d",d->alt_ft);
            if(d->valid&VALID_GS) snprintf(spd,sizeof(spd),"%d",d->speed_kts);
            if(d->valid&VALID_TRACK) snprintf(hdg,sizeof(hdg),"%03d",d->heading_deg);
            if((d->valid&VALID_POS) && now_ms>=active[row]->pos_ms &&
               now_ms-active[row]->pos_ms<CPR_POS_MS) {
                snprintf(lat,sizeof(lat),"%.3f",d->lat);
                snprintf(lon,sizeof(lon),"%.3f",d->lon);
            }
            snprintf(line,sizeof(line)," %06X S     %-4s  %-8.8s %6s %4s  %3s  %7s %8s %5.1f %5u   %2u",
                     d->addr,sq,d->callsign,alt,spd,hdg,lat,lon,d->rssi_db,
                     d->messages,(unsigned)((now_ms-d->seen_ms)/1000ULL));
            paint(row+2U,line);
        }
    }
    fflush(stdout);
}
void lite_interactive_end(void) {
    if (enabled && screen) {
        /* Place final CLI output below the dashboard, restore normal line flow. */
        fputs("\x1b[24;1H\x1b[K\n",stdout);
        fflush(stdout);
    }
#ifdef READSB_AMIGA
    if (interactive_raw_active) {
        /* Discard bounded type-ahead in RAW mode; it must not become a Shell
         * command after normal input mode is restored. Never block on input. */
        unsigned batches;
        for (batches=0; batches<8U; ++batches) {
            if (!WaitForChar(interactive_input,0)) break;
            lite_interactive_discard_input();
        }
        SetMode(interactive_input,0);
        interactive_raw_active=0;
        interactive_input=0;
    }
#endif
    enabled=0;
    screen=0;
}
