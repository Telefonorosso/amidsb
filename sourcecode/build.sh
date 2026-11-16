#!/usr/bin/env bash
# readsb-Amiga PORT39 FIX1: minimal build for hardware-validated sources.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mode="${1:-amiga}"
case "$mode" in amiga|host) ;; *) echo 'Usage: ./build.sh [amiga|host]' >&2; exit 2 ;; esac
src="$root/src"
local_src="$src/amiga"
upstream="$src/readsb"
: "${M68K_CC:=m68k-amigaos-gcc}"
: "${AMIPORT_ROOT:=/root/amiport}"
: "${READSB_DEPS_PREFIX:=/root/rtlsdr/port33-headers}"
: "${POSEIDON_INC:=/root/rtlsdr/poseidon-sdk/include}"
: "${RTLSDR_SRC:=/root/rtlsdr/rtl-sdr-upstream}"

if [[ "$mode" == amiga ]]; then
    cc="$M68K_CC"
    deps=( "$AMIPORT_ROOT/lib/posix-shim/include/amiport/unistd.h"
           "$AMIPORT_ROOT/lib/posix-shim/libamiport.a"
           "$READSB_DEPS_PREFIX/include/zlib.h"
           "$READSB_DEPS_PREFIX/include/zstd.h"
           "$POSEIDON_INC/proto/poseidon.h"
           "$RTLSDR_SRC/src/tuner_r82xx.c"
           "$RTLSDR_SRC/include/tuner_r82xx.h"
           "$RTLSDR_SRC/include/rtlsdr_i2c.h" )
    flags=( -std=gnu11 -m68020 -mcrt=nix20 -ffunction-sections -fdata-sections )
    cpp=( -D_GNU_SOURCE -D_DEFAULT_SOURCE -DDISABLE_INTERACTIVE=1
          -DREADSB_AMIGA=1 -DREADSB_LITE_AMIPORT=1
          "-I$AMIPORT_ROOT/lib/posix-shim/include"
          "-I$READSB_DEPS_PREFIX/include" )
    ldflags=( -m68020 -mcrt=nix20 -Wl,--gc-sections
              "$AMIPORT_ROOT/lib/posix-shim/libamiport.a" -lm -lamiga )
    outfile="$root/dist/READSB_PORT39F1"
else
    cc="${CC:-gcc}"
    flags=( -std=gnu11 -ffunction-sections -fdata-sections )
    cpp=( -D_GNU_SOURCE -D_DEFAULT_SOURCE -DDISABLE_INTERACTIVE=1 )
    ldflags=( -Wl,--gc-sections -lm -lpthread )
    outfile="$root/dist/readsb-lite-host"
    deps=()
fi
command -v "$cc" >/dev/null || { echo "Compiler not found: $cc" >&2; exit 2; }
for dep in "${deps[@]}"; do
    [[ -f "$dep" ]] || { echo "Missing dependency: $dep" >&2; exit 2; }
done
build="$root/build/$mode"
mkdir -p "$build" "$root/dist"
inc=( "-I$upstream" "-I$local_src" )
objs=()

# Exact original readsb DSP and CPR units; -O2 on the 68k target.
for unit in demod_2400 crc mode_s convert icao_filter mode_ac comm_b ais_charset cpr; do
    "$cc" "${cpp[@]}" "${inc[@]}" "${flags[@]}" -O2 \
        -c "$upstream/$unit.c" -o "$build/$unit.o"
    objs+=( "$build/$unit.o" )
done

# Match the hardware-validated target's optimization levels per file.
for unit in readsb_lite lite_interactive; do
    opt=-O2; [[ "$mode" == amiga ]] && opt=-O0
    "$cc" "${cpp[@]}" "${inc[@]}" "${flags[@]}" "$opt" \
        -c "$local_src/$unit.c" -o "$build/$unit.o"
    objs+=( "$build/$unit.o" )
done
"$cc" -std=gnu11 -O2 "${inc[@]}" -Wall -Wextra -Werror \
   -c "$local_src/gain_cli.c" -o "$build/gain_cli.o"
objs+=( "$build/gain_cli.o" )

if [[ "$mode" == amiga ]]; then
    for unit in rx_pipe rx_stream beast_fifo; do
        "$cc" -std=gnu11 -O2 -m68020 -mcrt=nix20 -Wall -Wextra -Werror \
            -c "$local_src/$unit.c" -o "$build/$unit.o"
        objs+=( "$build/$unit.o" )
    done
    "$cc" -std=gnu11 -O1 -m68020 -mcrt=nix20 -DREADSB_AMIGA=1 \
        -Wall -Wextra -Werror=implicit-function-declaration -Werror=int-conversion \
        -c "$local_src/beast_tcp.c" -o "$build/beast_tcp.o"
    objs+=( "$build/beast_tcp.o" )
    "$cc" -std=gnu99 -O1 -m68020 -mcrt=nix20 -Wall -Wextra \
        -Werror=implicit-function-declaration -Werror=int-conversion \
        "-I$POSEIDON_INC" "-I$RTLSDR_SRC/include" "-I$local_src" \
        -c "$local_src/rf1090_backend.c" -o "$build/rf1090_backend.o"
    objs+=( "$build/rf1090_backend.o" )
    "$cc" -std=gnu99 -O1 -m68020 -mcrt=nix20 -Wall -Wextra \
        -Werror=implicit-function-declaration -Werror=int-conversion \
        "-I$RTLSDR_SRC/include" \
        -c "$RTLSDR_SRC/src/tuner_r82xx.c" -o "$build/tuner_r82xx.o"
    objs+=( "$build/tuner_r82xx.o" )
fi

"$cc" "${objs[@]}" "${ldflags[@]}" -o "$outfile"
if [[ "$mode" == amiga ]]; then
    python3 - "$outfile" <<'PY'
from pathlib import Path
import sys
assert Path(sys.argv[1]).read_bytes()[:4] == bytes.fromhex('000003f3'), 'Missing Amiga HUNK header'
print('Amiga HUNK header: PASS')
PY
fi
printf 'Built: %s\n' "$outfile"
