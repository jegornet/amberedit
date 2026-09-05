#!/bin/bash
#
# Builds AmberEdit's Windows dependencies for x86_64-w64-mingw32, from Linux or
# macOS, into a prefix of their own.
#
# There is no distribution to ask for these — the Windows build has no package
# manager behind it the way the RPM and deb builds do — so they are built once
# here and named to CMake afterwards. Nothing is written into the source tree.
#
#   tools/build-w64-deps.sh
#   cmake -S . -B build-w64 -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake \
#         -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_PREFIX_PATH="$HOME/w64deps/prefix;$(brew --prefix tl-expected);$(brew --prefix doctest)"
#
# tl::expected and doctest are header-only and architecture independent, so the
# host's own copies serve and are not built here.
set -euo pipefail

HOST=${HOST:-x86_64-w64-mingw32}
ROOT=${AMBEREDIT_W64_ROOT:-$HOME/w64deps}
PREFIX="$ROOT/prefix"
SRC="$ROOT/src"
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
# Resolved before anything cds anywhere, since everything below does.
TOOLS=$(cd "$(dirname "$0")" && pwd)

ZLIB=zlib-1.3.1
ICONV=libiconv-1.18
GETTEXT=gettext-0.23.1

command -v "$HOST-gcc" >/dev/null || {
    echo "no $HOST-gcc on PATH — install mingw-w64 (brew install mingw-w64)" >&2
    exit 1
}

mkdir -p "$SRC" "$PREFIX"
cd "$SRC"

fetch() {  # fetch <url> <tarball> <directory>
    [ -d "$3" ] && return 0
    [ -f "$2" ] || curl -fsSL -o "$2" "$1"
    tar xzf "$2"
}

fetch "https://github.com/madler/zlib/releases/download/v1.3.1/$ZLIB.tar.gz" \
      "$ZLIB.tar.gz" "$ZLIB"
fetch "https://ftp.gnu.org/pub/gnu/libiconv/$ICONV.tar.gz" "$ICONV.tar.gz" "$ICONV"
fetch "https://ftp.gnu.org/pub/gnu/gettext/$GETTEXT.tar.gz" "$GETTEXT.tar.gz" "$GETTEXT"

[ -d PDCursesMod ] || git clone --depth 1 https://github.com/Bill-Gray/PDCursesMod.git

echo "=== zlib ==="
cd "$SRC/$ZLIB"
make -f win32/Makefile.gcc PREFIX="$HOST-" -j"$JOBS" libz.a >/dev/null
install -d "$PREFIX/lib" "$PREFIX/include"
install -m644 libz.a "$PREFIX/lib/"
install -m644 zlib.h zconf.h "$PREFIX/include/"

echo "=== libiconv ==="
cd "$SRC/$ICONV"
[ -f Makefile ] || ./configure --host="$HOST" --prefix="$PREFIX" \
    --enable-static --disable-shared --disable-nls >/dev/null
make -j"$JOBS" >/dev/null && make install >/dev/null

echo "=== gettext runtime (libintl) ==="
cd "$SRC/$GETTEXT/gettext-runtime"
[ -f Makefile ] || ./configure --host="$HOST" --prefix="$PREFIX" \
    --enable-static --disable-shared --disable-java --disable-csharp \
    --with-libiconv-prefix="$PREFIX" >/dev/null
make -j"$JOBS" >/dev/null && make install >/dev/null

# The wincon port. What it has to be built with, and why each of those matters,
# is in the script itself. A build on Windows installs MSYS2's package instead
# and never runs it.
echo "=== PDCursesMod (wincon, wide, UTF-8) ==="
AMBEREDIT_PDCURSES_SRC="$SRC/PDCursesMod" \
    "$TOOLS/build-pdcurses.sh" "$PREFIX" "$HOST"

echo
echo "done — $PREFIX"
ls -la "$PREFIX/lib"/*.a
