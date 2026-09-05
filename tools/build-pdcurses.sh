#!/bin/bash
#
# Builds PDCursesMod's wincon port into a prefix, for a build that has no package
# manager to ask for it: a cross-build from Linux or macOS. On Windows itself
# MSYS2 packages this library — mingw-w64-ucrt-x86_64-pdcurses, built wide and
# 64-bit as below — and INSTALL.md installs it with the rest of the dependencies.
#
#   tools/build-pdcurses.sh <prefix> [<host-triple>]
#
# With no triple it builds with whatever `gcc` and `ar` are on PATH; with one it
# cross-builds, which is how tools/build-w64-deps.sh calls it.
#
# What has to be right:
#
#   * a 64-bit chtype — the default, so CHTYPE_32 is simply never passed. It is
#     what carries the extended colour pairs and the combining-character scheme.
#     Without it the curses probe in CMakeLists.txt refuses the library.
#   * WIDE=Y — cchar_t and the wide API. Also caught by the probe.
#   * UTF8=Y — the library converts on the way out whatever the console code page
#     is, so what AmberEdit writes is what appears.
#
# The colour order is not on that list. PDC_RGB decides whether the first eight
# colours are numbered the ANSI way or the DOS way inside the library, and
# src/ui/term/color.cpp asks at run time which of the two it got and hands it
# numbers to suit — so a library built either way draws the themes as written.
set -euo pipefail

PREFIX=${1:?usage: build-pdcurses.sh <prefix> [<host-triple>]}
HOST=${2:-}
CROSS=${HOST:+$HOST-}
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)

SRC=${AMBEREDIT_PDCURSES_SRC:-$(dirname "$PREFIX")/src/PDCursesMod}
[ -d "$SRC" ] || git clone --depth 1 https://github.com/Bill-Gray/PDCursesMod.git "$SRC"

cd "$SRC/wincon"
make -f Makefile clean >/dev/null 2>&1 || true
make -f Makefile PREFIX="$CROSS" WIDE=Y UTF8=Y -j"$JOBS" >/dev/null

install -d "$PREFIX/lib" "$PREFIX/include"
install -m644 pdcurses.a "$PREFIX/lib/libpdcurses.a"
install -m644 ../curses.h ../panel.h "$PREFIX/include/"

echo "PDCursesMod -> $PREFIX/lib/libpdcurses.a"
