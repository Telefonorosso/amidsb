<p align="center">
  <img src="cover.jpg" alt="this shouldnt exist">
</p>

# amidsb - AI CODED

**Native ADS-B receiver for AmigaOS 68k, using an RTL-SDR dongle and Poseidon USB.**

Receive and track aircraft directly in an AmigaOS console, **without Linux, ncurses, or an Internet connection**, or send decoded Mode S / ADS-B messages to another receiver over Beast TCP.

## Requirements

- **AmigaOS 68k** with a sufficiently fast CPU. Hardware-tested on **Amiga 600 + PiStorm / Emu68 (Raspberry Pi 3A+)**.
- **Poseidon 6.2** USB stack, properly installed and configured.
- **`dwc2usb.device`** USB host controller driver for PiStorm.
- **RTL-SDR USB dongle** based on RTL2832U and an R820T/R820T2-class tuner.
  - Hardware-validated: **Nooelec NESDR SMArt**, USB VID:PID `0BDA:2838`
  - The current backend requires these identifiers. Other dongles are not yet validated.
- **1090 MHz ADS-B antenna** connected to the SDR dongle.
- **TCP/IP stack with `bsdsocket.library`** for `RTLBEAST` only. Hardware-tested with MiamiDX. A working LAN connection is required to reach the Beast TCP server on port `30005`.
- **Recommended AmigaDOS stack:** `262144` bytes (`Stack 262144` before launching the executable).

## Install and run

Copy the native Amiga executable `AMIDSB` to your Amiga and open a Shell:

```text
Stack 262144
AMIDSB RTLINTERACTIVE
```

**`GAIN=AUTO` is already the default**; you do not need to specify it.

Press **Ctrl-C** to stop. The program releases the SDR and restores the console's normal input mode.

## RF commands

### `RFSET` — check the receiver

```text
Stack 262144
AMIDSB RFSET
```

Initializes the RTL2832U and R82xx tuner, configures 1090 MHz / 2.4 Msps, and checks PLL lock. Inspect the `[USB]`, `[BIND]`, and `[RF]` messages for errors and `pll_lock=1`.

**It does not capture or decode signals.** It releases the device before exiting and is **not a required initialization step** before either receiver mode.

### `RTLINTERACTIVE` — standalone aircraft table

```text
Stack 262144
AMIDSB RTLINTERACTIVE
```

Receives and decodes ADS-B locally and displays a continuously updated aircraft table with ICAO addresses, callsigns, squawks, altitudes, speeds, headings, positions, RSSI, message counts, and `Seen` (seconds since the last received message).

- No ncurses or Linux companion computer required.
- No network stack required: **Beast TCP is disabled** in this mode.
- Ordinary keypresses do not pause the display; **Ctrl-C** stops reception.
- A valid latitude/longitude may take time to appear because the local tracker needs a suitable pair of airborne CPR messages.
- Up to 128 aircraft are tracked internally; up to 20 rows are displayed at a time.

### `RTLBEAST` — Beast TCP server

```text
Stack 262144
AMIDSB RTLBEAST
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
Stack 262144
AMIDSB RTLINTERACTIVE GAIN=AUTO
AMIDSB RTLINTERACTIVE GAIN=30
AMIDSB RTLBEAST GAIN=40.5
AMIDSB RFSET GAIN=30
```

| Setting | Behavior |
| --- | --- |
| Omitted | **Hardware R82xx AGC** (default) |
| `GAIN=AUTO` | Hardware R82xx AGC; identical to omitted |
| `GAIN=30` | Request 30.0 dB manual tuner gain |
| `GAIN=40.5` | Request 40.5 dB manual tuner gain |
| `GAIN=0` through `GAIN=49.6` | Manual gain accepted by the parser, in dB, with at most one decimal digit |

The tuner driver maps a requested manual gain to its supported hardware settings; not every intermediate value is necessarily applied exactly. Use uppercase `GAIN=`. `GAIN=auto` also works.

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

## Credits and Acknowledgements

**readsb-Amiga** builds upon the work of several open-source projects and their contributors:

- **[wiedehopf/readsb](https://github.com/wiedehopf/readsb)** — Original ADS-B/Mode S decoder, demodulation algorithms, message processing, CPR decoding and aircraft tracking. Special thanks to Matthias Wirth (*wiedehopf*) and the readsb contributors.
- **[dump1090](https://github.com/antirez/dump1090)** — Original work by Salvatore Sanfilippo, on which subsequent dump1090 and readsb developments are based.
- **[MalcolmRobb/dump1090](https://github.com/MalcolmRobb/dump1090)** and **[FlightAware/dump1090](https://github.com/flightaware/dump1090)** — Important contributions to the evolution of Mode S/ADS-B decoding.
- **[Osmocom rtl-sdr](https://github.com/osmocom/rtl-sdr)** — RTL2832U and Rafael R82xx tuner control code.
- **Poseidon USB Stack** — Native AmigaOS USB infrastructure enabling direct access to RTL-SDR hardware.
- **[amiport](https://github.com/bdgscotland/amiport)** — POSIX compatibility infrastructure supporting native AmigaOS software ports.
- **[PiStorm](https://github.com/captain-amygdala/pistorm)** and **[Emu68](https://github.com/michalsc/Emu68)** — Hardware and emulation technologies making this project practical on classic Amiga computers.

Special thanks to the Amiga development community for its documentation, tools and continued support of classic hardware.

### Licensing

AMIDSB is distributed under **GNU GPL v3 or later**, subject to the copyright notices and applicable licenses of the incorporated upstream components.
All original copyright notices and license files must be preserved when redistributing the source code or binaries.
