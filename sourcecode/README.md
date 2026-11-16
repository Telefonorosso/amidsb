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
