/*
 * readsb-Amiga POC4: physical 1090MHz RF receive via Poseidon 6.2.
 * USB controller / application binding paths derived from hardware-validated
 * RTLSDR-Poseidon POC2 STRESS16, maintained without ABI changes.
 * R820T2 tuning: upstream Osmocom r82xx tuner implementation, linked at build.
 * GPL-3.0-or-later for integration with upstream readsb slicers.
 * Integrated with original readsb demod_2400 via PORT34 Lite engine.
 * PORT34 RFSET, RTL4, RTL16 hardware-validated by user (2026-11-14).
 * PORT35 DSP timing validated; PORT36 ASYNCPIPE 16 MiB hardware validated.
 * PORT37 streaming mode is a NEW candidate requiring hardware validation.
 */
/*
 * Retains the original native USB, binding and baseband paths from
 * RTLSDR-Poseidon STRESS16 (hardware-validated). RF mode below is new.
 * GPL-3.0-or-later; historical STRESS16 integration GPL-2.0-or-later.
 */
#include <exec/types.h>

/* Compatibility with Poseidon SDK generated from AROS headers.
 * On native AmigaOS/m68k, pointers and ULONG are both 32-bit.
 * The generated clib prototypes mention IPTR without defining it in
 * some AmigaOS 3.x NDK combinations. This is an SDK declaration shim;
 * no system header or Poseidon library is modified.
 */
#ifndef IPTR
#define IPTR ULONG
#endif
#include <exec/libraries.h>
#include <exec/ports.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <proto/poseidon.h>
#include <libraries/poseidon.h>
#include <devices/usb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <tuner_r82xx.h>
#include <rtlsdr_i2c.h>
#include "lite_engine.h"
#include "lite_interactive.h"
#include "rx_pipe.h"
#include "rx_stream.h"
#include "beast_tcp.h"
#include <inttypes.h>

/* 68k amiga.lib C-hook bridge: A0=Hook, A2=object, A1=message -> C stack.
 * HookEntry is an assembler stub provided by amiga.lib (-lamiga).
 * It is only referenced as an entry pointer, never directly called here. */
extern ULONG HookEntry();

#define RTL_VID 0x0BDAUL
#define RTL_PID 0x2838UL
#define RTL_USB_BLOCK 1U
#define RTL_SYS_BLOCK 2U
#define RTL_I2C_BLOCK 6U
#define RF_SAMPLE_RATE 2400000UL
#define RF_CENTER_HZ 1090000000UL
#define RF_BYTES (16UL*1024UL*1024UL)
#define RF_CHUNK (64UL*1024UL)

/* Official Poseidon inline headers use PsdBase for their library base. */
struct Library *PsdBase = NULL;

struct Context {
    APTR dev, claim, ep0, epin;
    struct MsgPort *mp;
    unsigned long usb_error;
    int baseband_on;
    int repeater_on;
    int tuner_initialized;
    struct r82xx_config r82cfg;
    struct r82xx_priv r82;
    struct Hook release_hook;      /* live for the entire lifetime of binding */
    ULONG release_hook_calls;
};

/* Called by Poseidon under its device lock upon application binding release.
 * Do not call Poseidon, perform allocations, or do any I/O here.
 * On normal exit the pipes are freed BEFORE the binding is released. */
static ULONG release_callback(struct Hook *hook, APTR binding, APTR userdata)
{
    struct Context *c = (struct Context *)hook->h_Data;
    (void)binding;
    (void)userdata;
    if (c) {
        c->release_hook_calls++;
        c->claim = NULL;
    }
    return 0;
}

static struct TagItem device_tags[] = {
    { DA_VendorID,  RTL_VID },
    { DA_ProductID, RTL_PID },
    { TAG_DONE, 0 }
};
static struct TagItem if_tags[] = {
    { IFA_InterfaceNum, 0 },
    { IFA_AlternateNum, 0 },
    { TAG_DONE, 0 }
};
static struct TagItem ep_tags[] = {
    { EA_IsIn, TRUE },
    { EA_TransferType, USEAF_BULK },
    { EA_EndpointNum, 1 },
    { TAG_DONE, 0 }
};

/* Nonallocating wrapper. psdDoPipe returns IO error; use Actual for length. */
static int ctrl(struct Context *c, int in, UWORD value, UWORD index,
                UBYTE *data, ULONG len)
{
    LONG err;
    ULONG got;
    if (!c->ep0) return -1;
    psdPipeSetup(c->ep0, (in ? URTF_IN : URTF_OUT) | URTF_VENDOR | URTF_DEVICE,
                 0, value, index);
    err = psdDoPipe(c->ep0, data, len);
    c->usb_error = (ULONG) psdGetPipeError(c->ep0);
    got = psdGetPipeActual(c->ep0);
    if (err || c->usb_error || got != len) {
        printf("[CTRL] %s val=%04x idx=%04x err=%d pipe=%u got=%u expected=%u\n",
               in ? "IN" : "OUT", (ULONG)value, (ULONG)index,
               err, c->usb_error, got, len);
        return -1;
    }
    return 0;
}

static int reg_read(struct Context *c, UBYTE block, UWORD address, UBYTE *result)
{
    return ctrl(c, 1, address, (UWORD)((UWORD)block << 8), result, 1);
}

static int reg_write(struct Context *c, UBYTE block, UWORD address, UWORD val,
                     int size)
{
    UBYTE b[2];
    b[0] = (UBYTE)(size == 1 ? val : (val >> 8));
    b[1] = (UBYTE)val;
    return ctrl(c, 0, address, (UWORD)(((UWORD)block << 8) | 0x10),
                b, (ULONG)size);
}

static int demod_read(struct Context *c, UBYTE page, UBYTE reg, UBYTE *result)
{
    return ctrl(c, 1, (UWORD)(((UWORD)reg << 8) | 0x20), page, result, 1);
}

static int demod_write(struct Context *c, UBYTE page, UBYTE reg, UWORD val,
                       int size)
{
    UBYTE b[2], dummy = 0;
    int r;
    b[0] = (UBYTE)(size == 1 ? val : val >> 8);
    b[1] = (UBYTE)val;
    r = ctrl(c, 0, (UWORD)(((UWORD)reg << 8) | 0x20),
             (UWORD)(0x10 | page), b, (ULONG)size);
    if (r) return r;
    /* Required status read after demod register writes in librtlsdr. */
    return demod_read(c, 0x0a, 0x01, &dummy);
}

static int baseband_init(struct Context *c)
{
    static const short fir[16] = {
        -54,-36,-41,-40,-32,-14,14,53,101,156,215,273,327,372,404,421
    };
    UBYTE b[20];
    ULONG i;
    c->baseband_on = 1;  /* any failure still attempts powerdown */
#define WRBL(B,A,V,L) do { if (reg_write(c,(B),(A),(V),(L))) return -1; } while (0)
#define WRDM(P,R,V,L) do { if (demod_write(c,(P),(R),(V),(L))) return -1; } while (0)
    WRBL(RTL_USB_BLOCK,0x2000,0x09,1);
    WRBL(RTL_USB_BLOCK,0x2158,0x0002,2);
    WRBL(RTL_USB_BLOCK,0x2148,0x1002,2);
    WRBL(RTL_SYS_BLOCK,0x300b,0x22,1);
    WRBL(RTL_SYS_BLOCK,0x3000,0xe8,1);
    WRDM(1,0x01,0x14,1);
    WRDM(1,0x01,0x10,1);
    WRDM(1,0x15,0x00,1);
    WRDM(1,0x16,0x0000,2);
    for (i=0;i<6;i++) { WRDM(1,(UBYTE)(0x16+i),0,1); }
    for (i=0;i<8;i++) b[i] = (UBYTE)fir[i];
    for (i=0;i<8;i+=2) {
        UWORD f0 = (UWORD)fir[8+i], f1 = (UWORD)fir[8+i+1];
        ULONG o = 8+(i/2)*3;
        b[o]   = (UBYTE)(f0 >> 4);
        b[o+1] = (UBYTE)((f0 << 4) | ((f1 >> 8)&15));
        b[o+2] = (UBYTE)f1;
    }
    for (i=0;i<20;i++) { WRDM(1,(UBYTE)(0x1c+i),b[i],1); }
    WRDM(0,0x19,0x05,1);
    WRDM(1,0x93,0xf0,1);
    WRDM(1,0x94,0x0f,1);
    WRDM(1,0x11,0x00,1);
    WRDM(1,0x04,0x00,1);
    WRDM(0,0x61,0x60,1);
    WRDM(0,0x06,0x80,1);
    WRDM(1,0xb1,0x1b,1);
    WRDM(0,0x0d,0x83,1);

#undef WRBL
#undef WRDM
    return 0;
}


static void cleanup(struct Context *c)
{
    /* Stop tuner and disable I2C access BEFORE powering down RTL2832. */
    if (c->baseband_on && c->ep0) {
        if (c->tuner_initialized) {
            if (!c->repeater_on && !demod_write(c, 1, 0x01, 0x18, 1))
                c->repeater_on = 1;
            if (c->repeater_on) (void)r82xx_standby(&c->r82);
            c->tuner_initialized = 0;
        }
        if (c->repeater_on) {
            (void)demod_write(c, 1, 0x01, 0x10, 1);
            c->repeater_on = 0;
        }
        (void)demod_write(c,0,0x19,0x05,1);
        (void)reg_write(c,RTL_SYS_BLOCK,0x3000,0x20,1);
        c->baseband_on=0;
    }
    if (c->epin) { psdFreePipe(c->epin); c->epin=NULL; }
    if (c->ep0) { psdFreePipe(c->ep0); c->ep0=NULL; }
    if (c->mp) { DeleteMsgPort(c->mp); c->mp=NULL; }
    if (c->claim) {
        APTR pab = c->claim;
        /* The callback sets c->claim=NULL synchronously during release. */
        psdReleaseAppBinding(pab);
        c->claim = NULL;
        printf("[BIND] ReleaseHook calls=%u\n", (unsigned int)c->release_hook_calls);
        if (c->release_hook_calls != 1)
            puts("[BIND] WARNING: expected exactly one ReleaseHook call");
    } else if (c->release_hook_calls) {
        printf("[BIND] Binding was released externally; hook calls=%u\n",
               (unsigned int)c->release_hook_calls);
    }
    if (PsdBase) { CloseLibrary(PsdBase); PsdBase=NULL; }
}

static int start(struct Context *c, int require_claim)
{
    APTR pif, pep;
    APTR device_binding = NULL, interface_binding = NULL;
    ULONG vid=0, pid=0, configured=0, highspeed=0, ifnum=0, epnum=0;
    ULONG has_app_binding=0;
    LONG attrs_read;
    const char *name=NULL;
    struct TagItem claim_tags[] = {
        { ABA_Device, 0 },
        { ABA_ReleaseHook, 0 },
        { ABA_UserData, 0 },
        { ABA_ForceRelease, FALSE },
        { TAG_DONE, 0 }
    };
    struct TagItem info_tags[] = {
        { DA_VendorID, (ULONG)&vid },
        { DA_ProductID, (ULONG)&pid },
        { DA_IsConfigured, (ULONG)&configured },
        { DA_IsHighspeed, (ULONG)&highspeed },
        { DA_ProductName, (ULONG)&name },
        { DA_HasAppBinding, (ULONG)&has_app_binding },
        { DA_Binding, (ULONG)&device_binding },
        { TAG_DONE, 0 }
    };
    struct TagItem ifinfo_tags[] = {
        { IFA_InterfaceNum, (ULONG)&ifnum },
        { IFA_Binding, (ULONG)&interface_binding },
        { TAG_DONE, 0 }
    };
    struct TagItem epinfo_tags[] = {
        { EA_EndpointNum, (ULONG)&epnum }, { TAG_DONE, 0 }
    };

    /* 6.2 is built as library V6; require the V6 function table. */
    PsdBase = OpenLibrary("poseidon.library", 6);
    if (!PsdBase) { puts("[USB] poseidon.library V6+ not opened"); return -1; }
    printf("[USB] poseidon.library %u.%u\n",
           (unsigned int)PsdBase->lib_Version,
           (unsigned int)PsdBase->lib_Revision);
    c->dev = psdFindDeviceA(NULL, device_tags);
    if (!c->dev) { puts("[USB] 0BDA:2838 not found"); return -1; }
    attrs_read = psdGetAttrsA(PGA_DEVICE,c->dev,info_tags);
    printf("[USB] psdGetAttrsA(device)=%d\n", (int)attrs_read);
    printf("[USB] Device %04x:%04x %s configured=%u highspeed=%u\n",
           (unsigned int)vid,(unsigned int)pid,name ? name : "(unnamed)",
           (unsigned int)!!configured, (unsigned int)!!highspeed);
    printf("[BIND] Before: hasAppBinding=%u deviceBinding=%p\n",
           (unsigned int)!!has_app_binding, (void *)device_binding);
    if (vid!=RTL_VID || pid!=RTL_PID || !configured) {
        puts("[USB] Expected configured Realtek 0BDA:2838"); return -1;
    }
    pif = psdFindInterfaceA(c->dev,NULL,if_tags);
    if (!pif) { puts("[USB] Interface 0 not found"); return -1; }
    attrs_read = psdGetAttrsA(PGA_INTERFACE,pif,ifinfo_tags);
    printf("[USB] psdGetAttrsA(interface)=%d\n", (int)attrs_read);
    pep = psdFindEndpointA(pif,NULL,ep_tags);
    if (!pep) { puts("[USB] Bulk IN endpoint 1 not found"); return -1; }
    psdGetAttrsA(PGA_ENDPOINT,pep,epinfo_tags);
    printf("[USB] Interface=%u Bulk-IN endpoint=%u interfaceBinding=%p\n",
           (unsigned int)ifnum, (unsigned int)epnum, (void *)interface_binding);

    if (!require_claim) {
        puts("[PROBE] PASS: read-only enumeration + interface + bulk endpoint descriptors");
        puts("[PROBE] No device claimed, no pipes allocated, no RTL registers changed");
        return 0;
    }
    if (has_app_binding || device_binding || interface_binding) {
        puts("[BIND] WARNING: existing device/interface/application binding detected");
        puts("[BIND] Refusing to force-release another binding");
        return -1;
    }
    /* Poseidon 6.2 source unconditionally REQUIRES ABA_ReleaseHook.
     * Only an ABI-correct hook trampoline may be passed in h_Entry. */
    memset(&c->release_hook, 0, sizeof(c->release_hook));
    c->release_hook.h_Entry = (ULONG (*)())HookEntry;
    c->release_hook.h_SubEntry = (ULONG (*)())release_callback;
    c->release_hook.h_Data = (APTR)c;
    claim_tags[0].ti_Data = (ULONG)c->dev;
    claim_tags[1].ti_Data = (ULONG)&c->release_hook;
    claim_tags[2].ti_Data = (ULONG)c;
    printf("[BIND] Request: device=%p task=%p ReleaseHook=%p force=0\n",
           (void *)c->dev, (void *)FindTask(NULL), (void *)&c->release_hook);
    c->claim = psdClaimAppBindingA(claim_tags);
    if (!c->claim) {
        puts("[BIND] FAIL: psdClaimAppBindingA returned NULL");
        puts("[BIND] Claim failed despite ReleaseHook; check parent hub class and Poseidon log.");
        return -1;
    }
    puts("[BIND] Application binding acquired with valid ReleaseHook");
    c->mp=CreateMsgPort();
    if (!c->mp) return -1;
    c->ep0=psdAllocPipe(c->dev,c->mp,NULL);
    c->epin=psdAllocPipe(c->dev,c->mp,pep);
    if (!c->ep0 || !c->epin) { puts("[USB] Cannot allocate pipes"); return -1; }
    puts("[BIND] PASS: application binding + EP0 + Bulk-IN pipes");
    return 0;
}


/* Osmocom r82xx.c callback shims. Device-specific, no Linux/libusb required.
 * The I2C repeater must remain enabled for all tuner calls. */
int rtlsdr_check_dongle_model(void *dev, char *manuf, char *product)
{
    (void)dev; (void)manuf; (void)product;
    return 0; /* Nooelec SMArt, NOT an RTL-SDR Blog V4 variant. */
}
int rtlsdr_set_bias_tee_gpio(void *dev, int gpio, int on)
{
    (void)dev; (void)gpio; (void)on;
    return -1; /* fail closed; this device must NOT enable bias tee GPIO */
}
uint32_t rtlsdr_get_tuner_clock(void *dev)
{
    (void)dev;
    return 28800000U; /* Nooelec SMArt 28.8 MHz nominal crystal. */
}
int rtlsdr_i2c_write_fn(void *dev, uint8_t addr, uint8_t *buf, int len)
{
    struct Context *c = (struct Context *)dev;
    if (!c || !c->repeater_on || !buf || len <= 0 || len > 64) return -1;
    return ctrl(c, 0, (UWORD)addr, 0x0610U, (UBYTE *)buf, (ULONG)len) ? -1 : len;
}
int rtlsdr_i2c_read_fn(void *dev, uint8_t addr, uint8_t *buf, int len)
{
    struct Context *c = (struct Context *)dev;
    if (!c || !c->repeater_on || !buf || len <= 0 || len > 64) return -1;
    return ctrl(c, 1, (UWORD)addr, 0x0600U, (UBYTE *)buf, (ULONG)len) ? -1 : len;
}

static int rf_repeater(struct Context *c, int enabled)
{
    if (demod_write(c, 1, 0x01, enabled ? 0x18 : 0x10, 1)) return -1;
    c->repeater_on = enabled;
    return 0;
}
static int rf_if_frequency(struct Context *c, ULONG if_hz)
{
    int32_t ifcode = -(int32_t)((((uint64_t)if_hz) << 22) / 28800000ULL);
    printf("[RF] RTL IF=%lu Hz signed_ddc=%ld\n",
           (unsigned long)if_hz, (long)ifcode);
    if (demod_write(c, 1, 0x19, (UWORD)((ifcode >> 16) & 0x3f), 1) ||
        demod_write(c, 1, 0x1a, (UWORD)((ifcode >> 8) & 0xff), 1) ||
        demod_write(c, 1, 0x1b, (UWORD)(ifcode & 0xff), 1)) return -1;
    return 0;
}
static int rf_configure(struct Context *c, int gain_manual, int gain_tenths_db)
{
    UBYTE addr=0, id=0;
    int if_hz, rc;
    const ULONG ratio=0x03000000UL; /* 28.8MHz * 2^22 / 2.4MHz */
    if (baseband_init(c)) { puts("[RF] FAIL baseband init");return -1; }
    if (demod_write(c, 1, 0x9f, (UWORD)(ratio>>16), 2) ||
        demod_write(c, 1, 0xa1, (UWORD)(ratio & 0xffff), 2) ||
        demod_write(c, 1, 0x3f, 0, 1) ||
        demod_write(c, 1, 0x3e, 0, 1) ||
        demod_write(c, 1, 0x01, 0x14, 1) ||
        demod_write(c, 1, 0x01, 0x10, 1)) {
        puts("[RF] FAIL resampler init"); return -1;
    }
    printf("[RF] RTL2832U sample-rate=2400000 ratio=0x%08lx\n",
           (unsigned long)ratio);
    /* R820T2 uses low-IF, not the zero-IF path of the test generator. */
    if (demod_write(c, 1, 0xb1, 0x1a, 1) ||
        demod_write(c, 0, 0x08, 0x4d, 1) ||
        demod_write(c, 1, 0x15, 0x01, 1)) {
        puts("[RF] FAIL low-IF configuration");return -1;
    }
    if (rf_repeater(c,1)) { puts("[RF] FAIL I2C repeater");return -1; }
    if (rtlsdr_i2c_write_fn(c, 0x34, &addr, 1) != 1 ||
        rtlsdr_i2c_read_fn(c, 0x34, &id, 1) != 1 || id != 0x69) {
        printf("[RF] FAIL tuner id expected 0x69 got 0x%02x\n",(unsigned)id);
        return -1;
    }
    puts("[RF] R82xx tuner ID=0x69");
    memset(&c->r82cfg,0,sizeof(c->r82cfg));
    memset(&c->r82,0,sizeof(c->r82));
    c->r82cfg.i2c_addr=0x34;
    c->r82cfg.xtal=28800000U;
    c->r82cfg.rafael_chip=CHIP_R820T;
    c->r82cfg.max_i2c_msg_len=8; /* split large register writes safely */
    c->r82cfg.use_predetect=0;
    c->r82.cfg=&c->r82cfg;
    c->r82.rtl_dev=c;
    c->r82.int_freq=R82XX_IF_FREQ;
    rc=r82xx_init(&c->r82);
    if (rc < 0) { printf("[RF] FAIL r82xx_init=%d\n",rc);return -1; }
    c->tuner_initialized=1;
    if_hz=r82xx_set_bandwidth(&c->r82,(int)RF_SAMPLE_RATE,RF_SAMPLE_RATE);
    if (if_hz<=0 || if_hz>10000000) {
        printf("[RF] FAIL tuner bandwidth/IF=%d\n",if_hz);return -1;
    }
    if (rf_if_frequency(c,(ULONG)if_hz)) return -1;
    /* PORT38 FIX1: true tuner analog AGC is the default. This is NOT
     * RTL2832 digital AGC, which remains unchanged. */
    rc=r82xx_set_gain(&c->r82,gain_manual,gain_manual ? gain_tenths_db : 0);
    if (rc < 0) { printf("[RF] FAIL gain rc=%d\n",rc);return -1; }
    if (gain_manual)
        printf("[RF] R82xx gain=MANUAL requested=%d.%d dB\n",
               gain_tenths_db/10,gain_tenths_db%10);
    else
        puts("[RF] R82xx gain=AUTO (tuner analog AGC)");
    rc=r82xx_set_freq(&c->r82,RF_CENTER_HZ);
    printf("[RF] R82xx tune=%luHz rc=%d pll_lock=%d\n",
           (unsigned long)RF_CENTER_HZ,rc,c->r82.has_lock);
    /* Local driver shadow, not a physical readback. Confirm which gain
     * configuration the linked tuner driver attempted to set. */
    printf("[AGC/REG] shadow R05=0x%02x R07=0x%02x R0C=0x%02x\n",
           (unsigned)c->r82.regs[0], (unsigned)c->r82.regs[2],
           (unsigned)c->r82.regs[7]);
    if (rc < 0 || !c->r82.has_lock) {
        puts("[RF] FAIL: PLL not locked, refusing capture"); return -1;
    }
    if (rf_repeater(c,0)) return -1;
    /* Re-assert RF mode, then flush endpoint FIFO before reading. */
    if (demod_write(c,0,0x19,0x05,1) ||
        reg_write(c,RTL_USB_BLOCK,0x2148,0x1002,2) ||
        reg_write(c,RTL_USB_BLOCK,0x2148,0x0000,2)) return -1;
    puts("[RF] RF1090 CONFIGURED; TEST PATTERN DISABLED; PLL LOCKED");
    return 0;
}

static ULONG elapsed_ticks(const struct DateStamp *a, const struct DateStamp *b)
{
    LONG days=b->ds_Days-a->ds_Days, mins=b->ds_Minute-a->ds_Minute;
    LONG ticks=b->ds_Tick-a->ds_Tick;
    LONG total=days*4320000L+mins*3000L+ticks;
    return total>0?(ULONG)total:0;
}
/* Capture uninterrupted UC8 RF to guarded FAST RAM, then run the same
 * original readsb Lite decoder used by the hardware-validated file tests.
 * The capture/decode gap is intentional: this POC is not gapless. */
static int capture_rf(struct Context *c, ULONG requested, int save, int quiet)
{
    UBYTE *alloc=NULL, *iq=NULL;
    ULONG off=0, got=0, ticks=0, hist_ticks=0, dsp_ticks=0, i, nzero=0, nff=0;
    LONG usb_rc=0, pipe_error=0;
    FILE *fp=NULL;
    int status=-1;
    struct DateStamp begin,end,hist_begin,hist_end,dsp_begin,dsp_end;
    struct TagItem pipeopts[]={
        {PPA_NakTimeout,TRUE}, {PPA_NakTimeoutTime,2000},
        {PPA_AllowRuntPackets,TRUE}, {TAG_DONE,0}
    };
    if (requested!=4UL*1024UL*1024UL && requested!=RF_BYTES) return -1;
    alloc=(UBYTE *)malloc(requested+128U);
    if (!alloc) { puts("[RF] FAIL cannot allocate RAM buffer");return -1; }
    memset(alloc,0xa5,requested+128U);
    iq=alloc+64;
    /* Caller has already configured the tuner. */
    psdSetAttrsA(PGA_PIPE,c->epin,pipeopts);
    printf("[RF] Capture start: bytes=%lu chunk=%lu requests=%lu\n",
        (unsigned long)requested,(unsigned long)RF_CHUNK,(unsigned long)(requested/RF_CHUNK));
    DateStamp(&begin);
    /* Zero decoding, output, allocation or file I/O during USB capture. */
    while (off<requested) {
        usb_rc=psdDoPipe(c->epin,iq+off,RF_CHUNK);
        pipe_error=psdGetPipeError(c->epin);
        got=psdGetPipeActual(c->epin);
        if (usb_rc || pipe_error || got!=RF_CHUNK) break;
        off+=got;
    }
    DateStamp(&end);
    ticks=elapsed_ticks(&begin,&end);
    printf("[RF] Bulk-IN %lu/%lu bytes, elapsed=%lu ticks (50Hz)\n",
        (unsigned long)off,(unsigned long)requested,(unsigned long)ticks);
    if (ticks) printf("[RF] Throughput=%lu KiB/s\n",
        (unsigned long)(((uint64_t)off*50ULL)/(1024ULL*ticks)));
    if (off!=requested) {
        printf("[RF] FAIL USB err=%ld pipe=%ld got=%lu\n",
            (long)usb_rc,(long)pipe_error,(unsigned long)got);
        goto done;
    }
    for(i=0;i<64;i++) {
        if(alloc[i]!=0xa5 || alloc[64+requested+i]!=0xa5) {
            puts("[RF] FAIL memory guard corrupted"); goto done;
        }
    }
    DateStamp(&hist_begin);
    for(i=0;i<requested;i++) {
        nzero += (iq[i]==0);
        nff += (iq[i]==255);
    }
    DateStamp(&hist_end);
    hist_ticks=elapsed_ticks(&hist_begin,&hist_end);
    printf("[RF] IQ histogram: zero=%lu ff=%lu (real RF, no test counter)\n",
           (unsigned long)nzero,(unsigned long)nff);
    if (save) {
        const char *filename="RAM:readsb1090.iq";
        fp=fopen(filename,"wb");
        if (!fp || fwrite(iq,1,requested,fp)!=requested) {
            puts("[RF] FAIL writing RAM:readsb1090.iq"); goto done;
        }
        fclose(fp);fp=NULL;
        puts("[RF] Saved RAM:readsb1090.iq (raw interleaved UC8)");
    }
    lite_engine_set_quiet(quiet);
    DateStamp(&dsp_begin);
    if (lite_engine_begin()) { puts("[LITE/RF] FAIL decoder init"); goto done; }
    status=0;
    /* Send exact captured bytes in 64 KiB chunks. The engine retains
     * 384-sample overlap, original demod_2400 and CRC correction. */
    for(i=0;i<requested;i+=RF_CHUNK) {
        if (lite_engine_feed(iq+i,RF_CHUNK)) { status=-1; break; }
    }
    lite_engine_end(status);
    DateStamp(&dsp_end);
    dsp_ticks=elapsed_ticks(&dsp_begin,&dsp_end);
    printf("[PERF] USB=%lu ticks (%lu ms), histogram=%lu ticks (%lu ms)\n",
           (unsigned long)ticks,(unsigned long)(ticks*20U),
           (unsigned long)hist_ticks,(unsigned long)(hist_ticks*20U));
    printf("[PERF] DSP=%lu ticks (%lu ms), per-frame console=%s\n",
           (unsigned long)dsp_ticks,(unsigned long)(dsp_ticks*20U),
           quiet ? "OFF" : "ON");
    if (dsp_ticks && ticks) {
        ULONG percent=(ULONG)(((uint64_t)dsp_ticks*100U)/ticks);
        ULONG samples_per_second=(ULONG)((((uint64_t)requested/2U)*50U)/dsp_ticks);
        printf("[PERF] DSP/USB=%lu%%; DSP rate=%lu complex samples/s; required=2400000\n",
               (unsigned long)percent,(unsigned long)samples_per_second);
        puts(percent <= 100U
             ? "[PERF] Candidate DSP budget <= USB duration; NOT proof of gapless capture"
             : "[PERF] DSP currently slower than capture; needs optimization before gapless capture");
    } else {
        puts("[PERF] Timing below 1 tick or zero USB elapsed; ratio unavailable");
    }
    if (status) puts("[LITE/RF] FAIL decode");
    else puts("[LITE/RF] Burst decoded; zero frames does not indicate USB failure");
done:
    if(fp) fclose(fp);
    free(alloc);
    return status;
}

/* PORT36 POC1. One in-flight Poseidon Bulk-IN on the SAME pipe and task.
 * Two 64 KiB ping-pong buffers; demodulation overlaps the NEXT USB request.
 * Not a promise of lossless RF: hardware FIFO and host scheduling still matter.
 * The PORT35 synchronous RTL16Q branch above is left untouched. */
struct live_pipe_ctx {
    struct Context *usb;
    ULONG failed_error;
    ULONG failed_actual;
    LONG failed_wait;
};
static int live_pipe_send(void *arg, unsigned char *dst, unsigned long bytes)
{
    struct live_pipe_ctx *p = (struct live_pipe_ctx *)arg;
    if (!p->usb->epin || bytes != RF_CHUNK) return -1;
    psdSendPipe(p->usb->epin, dst, (ULONG)bytes);
    return 0;
}
static int live_pipe_wait(void *arg, unsigned long *actual)
{
    struct live_pipe_ctx *p = (struct live_pipe_ctx *)arg;
    LONG err = psdWaitPipe(p->usb->epin);
    LONG perr = psdGetPipeError(p->usb->epin);
    ULONG got = psdGetPipeActual(p->usb->epin);
    *actual = (unsigned long)got;
    if (err || perr || got != RF_CHUNK) {
        p->failed_wait=err;
        p->failed_error=(ULONG)perr;
        p->failed_actual=got;
        return -1;
    }
    return 0;
}
static void live_pipe_abort(void *arg)
{
    struct live_pipe_ctx *p = (struct live_pipe_ctx *)arg;
    psdAbortPipe(p->usb->epin);
    /* Reap completion before freeing buffers or pipes. */
    (void)psdWaitPipe(p->usb->epin);
}
static int live_pipe_consume(void *arg, const unsigned char *buf, unsigned long bytes)
{
    (void)arg;
    return lite_engine_feed(buf, (size_t)bytes);
}
static int capture_rf_pipe(struct Context *c, ULONG requested)
{
    UBYTE *allocation = NULL;
    UBYTE *buffer0, *buffer1;
    ULONG ticks = 0;
    int engine_started = 0;
    int rc = -1;
    struct DateStamp start_ts, end_ts;
    struct live_pipe_ctx live;
    struct lite_pipe_stats stats;
    static const struct lite_pipe_ops ops = {
        live_pipe_send, live_pipe_wait, live_pipe_abort, live_pipe_consume
    };
    struct TagItem pipeopts[] = {
        {PPA_NakTimeout,TRUE}, {PPA_NakTimeoutTime,2000},
        {PPA_AllowRuntPackets,TRUE}, {TAG_DONE,0}
    };
    if (requested != 4UL*1024UL*1024UL && requested != RF_BYTES) return -1;
    /* 2 x 64 KiB + sentinels, not a 4/16 MiB allocation. */
    allocation = (UBYTE *)malloc(RF_CHUNK*2UL + 192UL);
    if (!allocation) { puts("[PIPE] FAIL allocate ping-pong memory"); return -1; }
    memset(allocation,0xa5,RF_CHUNK*2UL+192UL);
    buffer0 = allocation+64;
    buffer1 = buffer0+RF_CHUNK+64;
    memset(&live,0,sizeof(live));
    memset(&stats,0,sizeof(stats));
    live.usb = c;
    lite_engine_set_quiet(1);
    if (lite_engine_begin()) { puts("[PIPE] FAIL decoder initialization"); goto done; }
    engine_started = 1;
    psdSetAttrsA(PGA_PIPE,c->epin,pipeopts);
    printf("[PIPE] Start: bytes=%lu chunk=%lu blocks=%lu buffers=2 inflight=1 quiet=ON\n",
           (unsigned long)requested, (unsigned long)RF_CHUNK,
           (unsigned long)(requested/RF_CHUNK));
    DateStamp(&start_ts);
    rc = lite_pipe_run(&ops, &live, buffer0, buffer1, RF_CHUNK,
                       requested/RF_CHUNK, &stats);
    DateStamp(&end_ts);
    ticks = elapsed_ticks(&start_ts,&end_ts);
    /* Engine is quiescent: no in-flight USB request when end() is called. */
    lite_engine_end(rc ? 1 : 0);
    engine_started = 0;
    printf("[PIPE] submitted=%lu completed=%lu delivered=%lu bytes=%lu aborted=%lu\n",
           stats.submitted,stats.completed,stats.delivered,stats.bytes,stats.aborted);
    printf("[PIPE] elapsed=%lu ticks (%lu ms), input_rate=%lu KiB/s\n",
           (unsigned long)ticks,(unsigned long)(ticks*20U),
           ticks ? (unsigned long)(((uint64_t)stats.bytes*50ULL)/(1024ULL*ticks)) : 0UL);
    if (rc) {
        printf("[PIPE] FAIL wait=%ld pipe=%lu actual=%lu\n",
               (long)live.failed_wait, (unsigned long)live.failed_error,
               (unsigned long)live.failed_actual);
    }
    if (allocation[0]!=0xa5 || allocation[63]!=0xa5 ||
        buffer0[RF_CHUNK]!=0xa5 || buffer0[RF_CHUNK+63]!=0xa5 ||
        buffer1[RF_CHUNK]!=0xa5 || buffer1[RF_CHUNK+63]!=0xa5) {
        puts("[PIPE] FAIL guard corruption"); rc=-1;
    }
    if (!rc && stats.bytes != requested) {
        puts("[PIPE] FAIL incomplete transfer"); rc=-1;
    }
    if (!rc) puts("[PIPE] POC1 complete; no host-side USB errors observed; gapless RF NOT YET PROVEN");
done:
    if (engine_started) lite_engine_end(1);
    free(allocation);
    return rc;
}


/* PORT37 POC1: readsb hex output WHILE RF capture is active. On Amiga,
 * printing each frame to a slow console can temporarily block the single
 * task and cause RTL2832 FIFO overrun, which cannot be directly detected.
 * Use RTLSTREAMQ / RTLSTREAM60Q to distinguish radio from console overhead.
 * DSP, tuner and Poseidon binding remain identical to PORT36. */
struct stream_ctx {
    struct live_pipe_ctx pipe; /* MUST be first: reuse validated callbacks */
    struct DateStamp start;
    ULONG elapsed;
    ULONG last_report_tick;
    uint64_t last_report_bytes;
    uint64_t last_report_frames;
    ULONG duration_ticks; /* zero = Ctrl-C-only */
    int stop_reason; /* 1 Ctrl-C, 2 timed */
    int beast; /* one-client native Beast TCP output, optional */
    int interactive; /* mutually exclusive with Beast; no network init */
    unsigned int report_count;
    int usb_only;  /* diagnostic: USB capture with zero DSP work */
    uint64_t iq_components, iq_rails, iq_near, iq_abs_dev;
};
/* Sample 1 complex pair in 64 from each received USB block. This bounded,
 * low-cost operation does not alter the samples or schedule. The returned
 * figures are APPROXIMATE due to subsampling. */
static int usb_only_consume(void *arg, const unsigned char *buf, unsigned long bytes)
{
    struct stream_ctx *s = (struct stream_ctx *)arg;
    unsigned long i;
    if (!buf || (bytes & 1UL)) return -1;
    for (i=0; i+1 < bytes; i+=128UL) {
        int x = buf[i], y = buf[i+1];
        s->iq_components += 2;
        s->iq_rails += (x <= 3 || x >= 252) + (y <= 3 || y >= 252);
        s->iq_near  += (x <= 16 || x >= 239) + (y <= 16 || y >= 239);
        s->iq_abs_dev += (unsigned)(x > 128 ? x-128 : 128-x) +
                         (unsigned)(y > 128 ? y-128 : 128-y);
    }
    return 0;
}
static void stream_sample_time(struct stream_ctx *s)
{
    struct DateStamp now;
    DateStamp(&now);
    s->elapsed = elapsed_ticks(&s->start, &now);
}
static int stream_stop(void *arg, const struct lite_stream_stats *st)
{
    struct stream_ctx *s = (struct stream_ctx *)arg;
    /* Check Ctrl-C on EVERY completed block, never inside a USB callback.
     * Clear break signal so cleanup/DOS output cannot be interrupted by it. */
    if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) {
        SetSignal(0, SIGBREAKF_CTRL_C);
        s->stop_reason = 1;
        return 1;
    }
    /* DateStamp costs one call every 16 blocks (~0.22 seconds). */
    if ((st->completed & 15ULL) == 0ULL) {
        stream_sample_time(s);
        if (s->duration_ticks && s->elapsed >= s->duration_ticks) {
            s->stop_reason = 2;
            return 1;
        }
    }
    return 0;
}
static void stream_report(void *arg, const struct lite_stream_stats *st)
{
    struct stream_ctx *s = (struct stream_ctx *)arg;
    uint64_t frames, dbytes;
    /* A newly submitted USB request is in flight while TCP is serviced.
     * All socket operations are nonblocking and have a strict byte budget. */
    if (s->beast) beast_tcp_poll();
    if (s->interactive) {
        if ((st->delivered & 15ULL) == 0ULL) {
            lite_interactive_discard_input(); /* bounded; no CON: line editor */
            ULONG dt=s->elapsed-s->last_report_tick;
            uint64_t dbytes=st->bytes-s->last_report_bytes;
            unsigned long rate=(dt>0) ? (unsigned long)((dbytes*50ULL)/(1024ULL*dt)) : 0;
            if (s->elapsed >= s->last_report_tick+50UL) {
                lite_interactive_render(lite_engine_sample_clock_ms(),rate);
                s->last_report_tick=s->elapsed;
                s->last_report_bytes=st->bytes;
            }
        }
        return; /* no diagnostic printf over the interactive screen */
    }
    ULONG dt;
    if ((st->delivered & 15ULL) != 0ULL) return;
    /* Time is refreshed in stream_stop every 16 blocks. */
    if (s->elapsed < s->last_report_tick ||
        s->elapsed - s->last_report_tick < 500UL) return;
    frames = lite_engine_decoded_total();
    dt = s->elapsed - s->last_report_tick;
    dbytes = st->bytes - s->last_report_bytes;
    printf("[STREAM] t=%lus blocks=%" PRIu64 " bytes=%" PRIu64
           " frames=%" PRIu64 " (+%" PRIu64 ") rate=%lu KiB/s host_errors=0\n",
           (unsigned long)(s->elapsed/50UL), st->delivered, st->bytes,
           frames, frames - s->last_report_frames,
           (unsigned long)((dbytes*50ULL)/(1024ULL*(uint64_t)dt)));
    if ((dbytes*50ULL)/(uint64_t)dt < 4500000ULL)
        puts("[STREAM] WARNING: effective transfer rate below 4.5 MB/s; check console/USB scheduling");
    if (s->usb_only && s->iq_components) {
        printf("[AGC/IQ] sampled_components=%" PRIu64
               " rails_per_mille=%lu near_per_mille=%lu mean_abs=%lu (subsampled)\n",
               s->iq_components,
               (unsigned long)(s->iq_rails*1000ULL/s->iq_components),
               (unsigned long)(s->iq_near*1000ULL/s->iq_components),
               (unsigned long)(s->iq_abs_dev/s->iq_components));
    } else if (!s->usb_only) {
        printf("[AGC/DSP] threshold=58 preambles=%" PRIu64
               " rejected=%" PRIu64 " (cumulative)\n",
               lite_engine_preambles_total(), lite_engine_rejected_total());
    }
    if (s->beast) {
        /* Upstream readsb uses Beast type-1 zero frames as heartbeats.
         * Keep slow TCP connectors alive even when indoor RF is silent. */
        if (++s->report_count % 6U == 0U) beast_tcp_heartbeat();
        beast_tcp_report();
    }
    s->last_report_tick = s->elapsed;
    s->last_report_bytes = st->bytes;
    s->last_report_frames = frames;
}
static int capture_rf_stream(struct Context *c, ULONG duration_seconds,
                             int quiet, int beast, int usb_only, int interactive)
{
    UBYTE *allocation = NULL, *b0, *b1;
    struct stream_ctx live;
    struct lite_stream_stats st;
    struct DateStamp end;
    ULONG ticks;
    int engine_started=0, rc=-1, beast_started=0;
    static const struct lite_stream_ops ops = {
        live_pipe_send, live_pipe_wait, live_pipe_abort, live_pipe_consume,
        stream_stop, stream_report
    };
    static const struct lite_stream_ops usb_ops = {
        live_pipe_send, live_pipe_wait, live_pipe_abort, usb_only_consume,
        stream_stop, stream_report
    };
    struct TagItem pipeopts[] = {
        {PPA_NakTimeout,TRUE}, {PPA_NakTimeoutTime,2000},
        {PPA_AllowRuntPackets,TRUE}, {TAG_DONE,0}
    };
    allocation = (UBYTE *)malloc(RF_CHUNK*2UL + 192UL);
    if (!allocation) { puts("[STREAM] FAIL allocate USB buffers"); return -1; }
    memset(allocation,0xa5,RF_CHUNK*2UL+192UL);
    b0=allocation+64;
    b1=b0+RF_CHUNK+64;
    memset(&live,0,sizeof(live));
    memset(&st,0,sizeof(st));
    live.pipe.usb=c;
    live.duration_ticks=duration_seconds*50UL;
    live.beast=beast;
    live.usb_only=usb_only;
    live.interactive=interactive;
    if (beast && interactive) { puts("[CLI] Beast and interactive are exclusive"); goto done; }
    if (beast) {
        if (beast_tcp_start(30005U)) goto done;
        beast_started=1;
    }
    if (!usb_only) {
        lite_engine_set_quiet(quiet);
        if (lite_engine_begin()) { puts("[STREAM] FAIL decoder initialization"); goto done; }
        engine_started=1;
    }
    if (interactive && !lite_interactive_begin(1)) {
        puts("[INTERACTIVE] Cannot set console RAW mode (SetMode/Input failed); aborting safely");
        goto done;
    }
    psdSetAttrsA(PGA_PIPE,c->epin,pipeopts);
    if (!interactive) printf("[STREAM] RUN 1090MHz 2.4Msps buffers=2 chunk=%lu inflight=1 output=%s limit=%s\n",
           (unsigned long)RF_CHUNK, quiet ? "STATS" : "READSB HEX+DF/ICAO/FIX",
           duration_seconds ? (duration_seconds == 60 ? "60s" : "300s") : "Ctrl-C");
    if (!interactive) puts("[STREAM] Stats every 10s; Ctrl-C to stop. No full-capture RAM allocation.");
    if (usb_only) puts("[AGC/DIAG] USB-ONLY: NO converter, NO demod, NO Beast; IQ sub-sampled for rails.");
    DateStamp(&live.start);
    /* A true streaming loop; no restart or resetting the SDR between blocks. */
    rc=lite_stream_run(usb_only ? &usb_ops : &ops,&live,b0,b1,RF_CHUNK,0,&st);
    DateStamp(&end);
    ticks=elapsed_ticks(&live.start,&end);
    /* lite_stream_run guarantees USB is reaped before decoder shutdown. */
    if (engine_started) {
        lite_engine_end(rc ? 1 : 0);
        engine_started=0;
    }
    if (interactive) lite_interactive_end();
    printf("[STREAM] stop=%s elapsed=%lu ticks (%lus) submitted=%" PRIu64
           " completed=%" PRIu64 " delivered=%" PRIu64
           " bytes=%" PRIu64 " frames=%" PRIu64 " aborted=%" PRIu64 "\n",
           live.stop_reason==1 ? "Ctrl-C" : live.stop_reason==2 ? "duration" : "error",
           (unsigned long)ticks,(unsigned long)(ticks/50UL),
           st.submitted,st.completed,st.delivered,st.bytes,
           lite_engine_decoded_total(),st.aborted);
    if (ticks) printf("[STREAM] avg_input=%lu KiB/s (nominal 4687 KiB/s)\n",
                      (unsigned long)((st.bytes*50ULL)/(1024ULL*(uint64_t)ticks)));
    if (rc) printf("[STREAM] FAIL wait=%ld pipe=%lu actual=%lu\n",
                   (long)live.pipe.failed_wait,(unsigned long)live.pipe.failed_error,
                   (unsigned long)live.pipe.failed_actual);
    if (beast_started) {
        /* Final overlap samples may have generated additional frames.
         * A small nonblocking drain lets the last queued packet leave. */
        unsigned int i;
        for (i=0; i<4U; ++i) beast_tcp_poll();
        beast_tcp_report();
    }
    if (allocation[0]!=0xa5 || allocation[63]!=0xa5 ||
        b0[RF_CHUNK]!=0xa5 || b0[RF_CHUNK+63]!=0xa5 ||
        b1[RF_CHUNK]!=0xa5 || b1[RF_CHUNK+63]!=0xa5) {
        puts("[STREAM] FAIL guard corruption");rc=-1;
    }
    if(!rc) puts(usb_only ?
                  "[USBRAW] OK: USB-only, no DSP; hardware FIFO gaps NOT measurable" :
                  "[STREAM] OK: host USB completions and DSP; hardware FIFO gaps NOT measurable");
done:
    if(engine_started) lite_engine_end(1);
    if (interactive) lite_interactive_end();
    if(beast_started) beast_tcp_stop();
    free(allocation);
    return rc;
}

/* Entry point selected by READSB_LITE main; only one application binding.
 * RFSET: configure/PLL diagnostic only
 * RTL4/RF1090SHORT: single 4 MiB capture+full readsb decode
 * RTL16/RF1090: single 16 MiB capture+decode
 * RTLSAVE/RF1090SAVE: 16 MiB + optional RAM: I/Q file
 * RTL/RTLLOOP: repeated 4 MiB bursts (gaps during decode) until Ctrl-C.
 */
int lite_rtl_run(const char *mode, int gain_manual, int gain_tenths_db)
{
    struct Context c;
    int rc=20, loop=0, save=0, quiet=0, streaming=0, beast=0, usb_only=0, interactive=0;
    ULONG duration_seconds=0;
    ULONG requested=4UL*1024UL*1024UL, burst=0;
    memset(&c,0,sizeof(c));
    puts("readsb-Amiga PORT39 INTERACTIVE POC1 / Poseidon 6.2 / R82xx / original readsb DSP");
    if(!strcmp(mode,"RFSET")) { /* no IQ capture */ }
    else if(!strcmp(mode,"RTL4") || !strcmp(mode,"RF1090SHORT")) { }
    else if(!strcmp(mode,"RTL4Q")) quiet=1;
    else if(!strcmp(mode,"RTL16") || !strcmp(mode,"RF1090")) requested=RF_BYTES;
    else if(!strcmp(mode,"RTL16Q")) { requested=RF_BYTES; quiet=1; }
    else if(!strcmp(mode,"RTLPIPE4Q")) { quiet=1; }
    else if(!strcmp(mode,"RTLPIPE16Q")) { requested=RF_BYTES; quiet=1; }
    else if(!strcmp(mode,"RTLSTREAM")) streaming=1;
    else if(!strcmp(mode,"RTLSTREAMQ")) { streaming=1; quiet=1; }
    else if(!strcmp(mode,"RTLSTREAM60")) { streaming=1; duration_seconds=60; }
    else if(!strcmp(mode,"RTLSTREAM60Q")) { streaming=1; quiet=1; duration_seconds=60; }
    else if(!strcmp(mode,"RTLINTERACTIVE")) { streaming=1; quiet=1; interactive=1; }
    else if(!strcmp(mode,"RTLINTERACTIVE60")) { streaming=1; quiet=1; interactive=1; duration_seconds=60; }
    else if(!strcmp(mode,"RTLUSB60Q")) { streaming=1; quiet=1; duration_seconds=60; usb_only=1; }
    else if(!strcmp(mode,"RTLSTREAM5")) { streaming=1; duration_seconds=300; }
    else if(!strcmp(mode,"RTLSTREAM5Q")) { streaming=1; quiet=1; duration_seconds=300; }
    else if(!strcmp(mode,"RTLBEAST")) { streaming=1; beast=1; }
    else if(!strcmp(mode,"RTLBEASTQ")) { streaming=1; beast=1; quiet=1; }
    else if(!strcmp(mode,"RTLBEAST60")) { streaming=1; beast=1; duration_seconds=60; }
    else if(!strcmp(mode,"RTLBEAST60Q")) { streaming=1; beast=1; quiet=1; duration_seconds=60; }
    else if(!strcmp(mode,"RTLBEAST5")) { streaming=1; beast=1; duration_seconds=300; }
    else if(!strcmp(mode,"RTLBEAST5Q")) { streaming=1; beast=1; quiet=1; duration_seconds=300; }
    else if(!strcmp(mode,"RTLSAVE") || !strcmp(mode,"RF1090SAVE")) {
        requested=RF_BYTES; save=1;
    }
    else if(!strcmp(mode,"RTL") || !strcmp(mode,"RTLLOOP")) loop=1;
    else { puts("[CLI] Unknown mode; refusing USB access"); return 10; }
    if(start(&c,1)) goto done;
    if(rf_configure(&c,gain_manual,gain_tenths_db)) goto done;
    if(!strcmp(mode,"RFSET")) {rc=0;goto done;}
    if(streaming) {rc=capture_rf_stream(&c,duration_seconds,quiet,beast,usb_only,interactive) ? 20 : 0;goto done;}
    do {
        burst++;
        printf("[RF] BURST %lu%s\n",(unsigned long)burst,
               loop ? " (non-gapless capture/decode cycle)" : "");
        rc=(!strcmp(mode,"RTLPIPE4Q") || !strcmp(mode,"RTLPIPE16Q")
              ? capture_rf_pipe(&c,requested) : capture_rf(&c,requested,save,quiet)) ? 20 : 0;
        if(rc) break;
        if(loop && (SetSignal(0,0) & SIGBREAKF_CTRL_C)) {
            puts("[RF] Ctrl-C detected; stopping after completed burst");
            SetSignal(0,SIGBREAKF_CTRL_C);
            break;
        }
    } while(loop);
done:
    cleanup(&c);
    return rc;
}
