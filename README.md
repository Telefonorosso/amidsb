# readsb-Amiga

**Native ADS-B receiver for AmigaOS 68k, using an RTL-SDR dongle and Poseidon USB.**

Receive and track aircraft directly in an AmigaOS console, **without Linux, ncurses, or an Internet connection**, or send decoded Mode S / ADS-B messages to another receiver over Beast TCP.

This README covers the hardware-validated **PORT39 INTERACTIVE FIX1** release and only its three primary RF commands: `RFSET`, `RTLINTERACTIVE`, and `RTLBEAST`.

## Requirements

- AmigaOS 68k with a sufficiently fast CPU (tested on **Amiga 600 + PiStorm / Emu68**).
- **Poseidon 6.2** USB stack and a working high-speed USB connection.
- RTL2832U-based SDR with R820T/R820T2-class tuner. **Hardware-validated dongle:** Nooelec NESDR SMArt, USB ID `0BDA:2838`. The current driver requires that USB ID and tuner ID `0x69`; other models are **not yet validated**.
- An antenna suitable for **1090 MHz ADS-B** reception.
- For `RTLINTERACTIVE`: a normal AmigaOS ANSI/CSI console, preferably **96+ columns** wide.
- For `RTLBEAST` only: a running AmigaOS TCP/IP stack providing `bsdsocket.library` (hardware-tested with MiamiDX) and LAN connectivity.

The receiver runs at **1090 MHz / 2.4 Msps**. These parameters are fixed in this release.

## Install and run

Copy the native Amiga executable `READSB_PORT39F1` to your Amiga and open a Shell:

```text
Stack 262144
READSB_PORT39F1 RTLINTERACTIVE
```

**`GAIN=AUTO` is already the default**; you do not need to specify it.

Press **Ctrl-C** to stop. The program releases the SDR and restores the console's normal input mode.

## RF commands

### `RFSET` — check the receiver

```text
READSB_PORT39F1 RFSET
```

Initializes the RTL2832U and R82xx tuner, configures 1090 MHz / 2.4 Msps, and checks PLL lock. Inspect the `[USB]`, `[BIND]`, and `[RF]` messages for errors and `pll_lock=1`.

**It does not capture or decode signals.** It releases the device before exiting and is **not a required initialization step** before either receiver mode.

### `RTLINTERACTIVE` — standalone aircraft table

```text
READSB_PORT39F1 RTLINTERACTIVE
```

Receives and decodes ADS-B locally and displays a continuously updated aircraft table with ICAO addresses, callsigns, squawks, altitudes, speeds, headings, positions, RSSI, message counts, and `Seen` (seconds since the last received message).

- No ncurses or Linux companion computer required.
- No network stack required: **Beast TCP is disabled** in this mode.
- Ordinary keypresses do not pause the display; **Ctrl-C** stops reception.
- A valid latitude/longitude may take time to appear because the local tracker needs a suitable pair of airborne CPR messages.
- Up to 128 aircraft are tracked internally; up to 20 rows are displayed at a time.

### `RTLBEAST` — Beast TCP server

```text
READSB_PORT39F1 RTLBEAST
```

Receives and decodes ADS-B, then serves binary Beast-format messages to **one TCP client** on **port 30005** (listens on `0.0.0.0`). This is a streaming mode, **not** the local interactive aircraft table.

On a Linux machine running readsb, connect to the Amiga's LAN address:

```sh
readsb --net --net-only --net-connector=AMIGA_IP,30005,beast_in
```

Replace `AMIGA_IP` with the Amiga's actual IP address. Start the Amiga's TCP/IP stack **before** launching `RTLBEAST`, and ensure TCP port 30005 is reachable from the Linux host.

The server does not require a username or password. **Use it on a trusted LAN; do not expose port 30005 directly to the Internet.** Press Ctrl-C to stop and close the listener.

`RTLINTERACTIVE` and `RTLBEAST` are **mutually exclusive**: choose one at startup.

## Gain control

Gain is an **optional second argument** after any of the three commands:

```text
READSB_PORT39F1 RTLINTERACTIVE GAIN=AUTO
READSB_PORT39F1 RTLINTERACTIVE GAIN=30
READSB_PORT39F1 RTLBEAST GAIN=40.5
READSB_PORT39F1 RFSET GAIN=30
```

| Setting | Behavior |
| --- | --- |
| Omitted | **Hardware R82xx AGC** (default) |
| `GAIN=AUTO` | Hardware R82xx AGC; identical to omitted |
| `GAIN=30` | Request 30.0 dB manual tuner gain |
| `GAIN=40.5` | Request 40.5 dB manual tuner gain |
| `GAIN=0` through `GAIN=49.6` | Manual gain accepted by the parser, in dB, with at most one decimal digit |

The tuner driver maps a requested manual gain to its supported hardware settings; not every intermediate value is necessarily applied exactly. Use uppercase `GAIN=`. `GAIN=auto` also works.

**Do not use `GAIN=-10`:** that is Linux readsb's AGC convention, not a valid argument for this Amiga build. Use `GAIN=AUTO` instead.

The original readsb DSP preamble threshold (**58**) remains unchanged in this release.

## Build from source (Debian cross-compiler)

The source package contains the Amiga port and build scripts. On a Debian machine with the existing `m68k-amigaos-gcc` toolchain, amiport library, Poseidon SDK, Osmocom R82xx tuner sources, and zlib/zstd target headers installed:

```sh
cd readsb-Amiga-PORT39-INTERACTIVE-FIX1
./VERIFY_PORT39.sh
./BUILD_PORT39.sh all
ls -lh dist/READSB_PORT39F1
```

The native executable is `dist/READSB_PORT39F1`. Copy that file to the Amiga. The build uses **`-O2` for the upstream DSP modules**, preserving the hardware-validated AGC performance configuration. No previous PORTxx ZIP is required; development toolchain and SDK dependencies are installed separately.

## Quick troubleshooting

| Symptom | Check |
| --- | --- |
| `[USB] 0BDA:2838 not found` | Dongle connected, detected by Poseidon, and correct USB ID |
| Application binding refused | Another USB driver/application may already own the dongle; inspect Trident/Poseidon |
| Tuner ID or PLL lock failure | USB power, R82xx compatibility, and the RF initialization log |
| No aircraft in `RTLINTERACTIVE` | 1090 MHz antenna placement, RF conditions, gain setting, and reception time |
| Linux cannot connect to Beast | TCP/IP stack active on Amiga, correct LAN IP, port 30005 reachable, and no existing Beast client |
| No latitude/longitude yet | Wait for enough valid airborne CPR messages from the same aircraft |

**Validation:** PORT39 INTERACTIVE FIX1 has been tested on real Amiga 600 / PiStorm / Emu68 hardware, including the local interactive display and Beast TCP mode. This does not imply validation of every RTL-SDR dongle or every TCP/IP stack.
