#!/bin/bash
#
# Builds PDCursesMod's wincon port into a prefix, with the four things AmberEdit
# needs it built with. One script rather than a line in each caller, because
# three of the four are quiet when they are missing.
#
#   tools/build-pdcurses.sh <prefix> [<host-triple>]
#
# With no triple it builds with whatever `gcc` and `ar` are on PATH, which is the
# native case — an MSYS2 shell on Windows. With one it cross-builds, which is how
# tools/build-w64-deps.sh calls it from Linux or macOS.
#
# What has to be right, and what happens when it is not:
#
#   * a 64-bit chtype — the default, so CHTYPE_32 is simply never passed. It is
#     what carries the extended colour pairs and the combining-character scheme.
#     Without it the curses probe in CMakeLists.txt refuses the library, which is
#     the one failure here that is loud.
#   * WIDE=Y — cchar_t and the wide API. Also caught by the probe.
#   * UTF8=Y — the library converts on the way out whatever the console code page
#     is, so what AmberEdit writes is what appears.
#   * PDC_RGB — the first eight colours in the ANSI order, 1 red and 4 blue.
#     PDCurses numbers them the DOS way otherwise, and a library built without
#     this draws a blue theme in red and a yellow one in cyan while reporting
#     nothing at all. It has to be defined for the library itself, since the
#     palette for entries 0-15 is built from these masks inside it — hence CC
#     rather than a flag on the side. src/ui/term/color.cpp asserts the other
#     half of this bargain, the one the compiler can see.
set -euo pipefail

PREFIX=${1:?usage: build-pdcurses.sh <prefix> [<host-triple>]}
HOST=${2:-}
CROSS=${HOST:+$HOST-}
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)

SRC=${AMBEREDIT_PDCURSES_SRC:-$(dirname "$PREFIX")/src/PDCursesMod}
[ -d "$SRC" ] || git clone --depth 1 https://github.com/Bill-Gray/PDCursesMod.git "$SRC"

cd "$SRC/wincon"
make -f Makefile clean >/dev/null 2>&1 || true
make -f Makefile PREFIX="$CROSS" CC="${CROSS}gcc -DPDC_RGB" WIDE=Y UTF8=Y -j"$JOBS" >/dev/null

install -d "$PREFIX/lib" "$PREFIX/include"
install -m644 pdcurses.a "$PREFIX/lib/libpdcurses.a"
install -m644 ../curses.h ../panel.h "$PREFIX/include/"

echo "PDCursesMod -> $PREFIX/lib/libpdcurses.a"
