# readsb-Amiga

Native **1090 MHz ADS-B / Mode S receiver for AmigaOS 68k**, based on
[wiedehopf/readsb](https://github.com/wiedehopf/readsb). Uses an RTL-SDR dongle
through **Poseidon 6.2**, with the original readsb decoder and a lightweight
local aircraft tracker.

**Hardware-validated:** Amiga 600 + PiStorm / Emu68 (Raspberry Pi 3A+),
Poseidon 6.2, Nooelec NESDR SMArt (RTL2838/R820T2), and MiamiDX for Beast TCP.
This source tree is the **PORT39 INTERACTIVE FIX1** baseline, including the
console RAW-input and `Seen` corrections. Other dongles and Amiga configurations
have not been validated.

## Run on AmigaOS

Set the process stack before launching:

```text
Stack 262144
```

| Command | Operation |
| --- | --- |
| `READSB_PORT39F1 RFSET` | Initialize/configure the RTL-SDR tuner, report PLL status, then exit. Diagnostic only; **not required** before the other modes. |
| `READSB_PORT39F1 RTLINTERACTIVE` | Standalone aircraft table in an AmigaOS console; **no ncurses, Linux host, or TCP/IP stack** required. |
| `READSB_PORT39F1 RTLBEAST` | Transmit Beast binary messages via a TCP server on **port 30005**; requires a supported `bsdsocket.library` stack. |

`RTLINTERACTIVE` and `RTLBEAST` are **mutually exclusive**. Press **Ctrl-C** to
stop a running receiver and release the USB device. An ANSI/CSI-capable console
at least 80 columns wide is required for the aircraft table (96+ recommended).

### Gain

**Hardware AGC is the default**. Omitting `GAIN` is equivalent to `GAIN=AUTO`:

```text
READSB_PORT39F1 RTLINTERACTIVE
READSB_PORT39F1 RTLINTERACTIVE GAIN=AUTO
READSB_PORT39F1 RTLINTERACTIVE GAIN=30
READSB_PORT39F1 RTLBEAST GAIN=40.5
```

`GAIN=<number>` requests a manual tuner gain in dB; the tuner selects a
supported gain setting. The Linux readsb `--gain -10` syntax is **not** accepted:
use `GAIN=AUTO` instead. The receiver operates at **1090 MHz / 2.4 Msps**.

## Requirements

**On the Amiga:** Poseidon 6.2 and a compatible USB host-controller driver
(e.g. [dwc2usb.device](https://github.com/LemaruX/dwc2usb.device) on the tested
PiStorm), an RTL2832U/R820T2 dongle with USB ID `0BDA:2838`, and a 1090 MHz
antenna. No separate RTL-SDR USB class or HID driver is used. `RTLBEAST` also
requires a TCP/IP stack exposing `bsdsocket.library` (tested with MiamiDX).

**On the build host:** Debian/Linux with `m68k-amigaos-gcc`, an AmigaOS NDK,
a built [amiport](https://github.com/bdgscotland/amiport) POSIX shim,
Poseidon 6.2 SDK headers, the Osmocom RTL-SDR **`tuner_r82xx.c`** source and
headers, and target zlib/zstd headers. These third-party SDKs and tuner sources
are **not bundled**. No earlier readsb-Amiga PORTxx archive is needed.

## Build

Set the dependencies' locations on your Debian cross-build machine:

```sh
export AMIPORT_ROOT=/root/amiport
export READSB_DEPS_PREFIX=/root/rtlsdr/port33-headers
export POSEIDON_INC=/root/rtlsdr/poseidon-sdk/include
export RTLSDR_SRC=/root/rtlsdr/rtl-sdr-upstream
export M68K_CC=m68k-amigaos-gcc

./build.sh amiga
```

Output: **`dist/READSB_PORT39F1`** (native AmigaOS HUNK). The script uses the
validated **`-O2` DSP configuration** and preserves the original optimization
levels of the USB, tracker, and Beast modules.

An optional host-only build for development is available with:

```sh
./build.sh host
```

Output: `dist/readsb-lite-host` (no RF hardware or USB back end). All relative
paths in the build are resolved from this repository; only external SDK/source
paths need configuring.

## Source layout

- [`src/amiga/readsb_lite.c`](src/amiga/readsb_lite.c) — decoder integration and message dispatch.
- [`src/amiga/rf1090_backend.c`](src/amiga/rf1090_backend.c) — Poseidon USB, RTL2832U control, and streaming.
- [`src/amiga/lite_interactive.c`](src/amiga/lite_interactive.c) — local aircraft tracking, CPR integration, ANSI/CSI output, RAW console input.
- [`src/amiga/beast_tcp.c`](src/amiga/beast_tcp.c) — Beast TCP service.
- [`src/amiga/gain_cli.c`](src/amiga/gain_cli.c) — RF gain parsing.
- [`src/readsb/`](src/readsb/) — only upstream readsb decoder units and the headers required to build them; no unrelated upstream applications.

All included application and readsb C/H files are copied **byte-for-byte** from
the hardware-validated PORT39 FIX1 source archive. This GitHub edition changes
only the directory layout and build script; it does **not** modify RF, DSP,
tracking, console, or network behavior.

## Credits and license

The decoder is derived from [wiedehopf/readsb](https://github.com/wiedehopf/readsb),
with foundations in dump1090 (Salvatore Sanfilippo, Oliver Jowett, Michael Wolf,
and other contributors). The R82xx tuner driver originates from
[Osmocom rtl-sdr](https://github.com/osmocom/rtl-sdr).
USB access is provided by Poseidon 6.2 and, on the tested PiStorm setup,
[LemaruX/dwc2usb.device](https://github.com/LemaruX/dwc2usb.device). The
[amiport](https://github.com/bdgscotland/amiport) project provides POSIX
compatibility at build/runtime.

See [`LICENSE`](LICENSE) for the upstream GPL text and retain the individual
copyright and license notices in source files. Any additional third-party
headers or source code used for compilation retain their respective licenses.
