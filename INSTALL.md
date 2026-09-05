# Installing AmberEdit

Every tagged release carries built packages —
[Releases](https://github.com/jegornet/amberedit/releases) has RPMs for RHEL 8,
9, 10 and Fedora, debs for Debian stable and Ubuntu 22.04, 24.04 and 26.04, a
package for Arch, and tarballs for macOS on both architectures. Building it
yourself is the rest of this file.

## What it needs

CMake ≥ 3.16, a C++17 compiler (GCC 8, Clang 7, Apple Clang 11), iconv (in libc
or libiconv), zlib, tl::expected, gettext and the wide-character ncurses, plus
doctest if the tests are to be built. Nothing is downloaded during the build.
There are no other dependencies.

tl::expected and doctest are header-only and wanted only while building; the
packages are `expected-devel` and `doctest-devel` on RHEL and Fedora,
`libexpected-dev` and `doctest-dev` on Debian and Ubuntu, `tl-expected` and
`doctest` in Arch's extra and in Homebrew.

gettext is two things here. Its `msgfmt` compiles `po/*.po` into the catalogs the
interface is drawn from, and that half is wanted only while building — without it
the build says so and goes on, and the interface is the English the program is
written in. Its runtime, `libintl`, is what reads a catalog, and that half is
required: on glibc it is part of libc and there is nothing to install, on macOS
and the BSDs it is a library of its own. The package is called `gettext`
everywhere named below.

One thing gettext needs is the system's rather than AmberEdit's: **the locale
you ask for has to exist**. A stock Debian or Ubuntu generates none at all — only
`C`, `C.UTF-8` and `POSIX`, which gettext treats as no locale — so
`apt install locales && locale-gen ru_RU.UTF-8` is what makes `LANG=ru_RU.UTF-8
amberedit` Russian. RHEL and Fedora ship enough already; where they do not,
`dnf install glibc-langpack-ru`. AmberEdit says so on startup when the language
asked for is one it has and the system cannot install, and runs in English
meanwhile.

That floor is chosen so that RHEL 8 and its rebuilds build AmberEdit out of the
box with nothing but the stock toolchain — no devtoolset, no newer CMake.

## Building

Install the dependencies with the one block that matches the system — the
blocks are alternatives, not steps — and then build.

**Fedora**

```bash
sudo dnf install gcc-c++ cmake git ncurses-devel zlib-devel \
                 expected-devel doctest-devel gettext glibc-gconv-extra
```

**RHEL 9 and its clones (Rocky, Alma)** — `expected-devel` and `doctest-devel` come
from EPEL:

```bash
sudo dnf install epel-release
sudo dnf install gcc-c++ cmake git ncurses-devel zlib-devel \
                 expected-devel doctest-devel gettext glibc-gconv-extra
```

**RHEL 10 and its clones** — the same, except that zlib is zlib-ng there and the
package carrying `zlib.h` is `zlib-ng-compat-devel`; there is no `zlib-devel` to
install:

```bash
sudo dnf install epel-release
sudo dnf install gcc-c++ cmake git ncurses-devel zlib-ng-compat-devel \
                 expected-devel doctest-devel gettext glibc-gconv-extra
```

**RHEL 8 and its clones** — the same, without `glibc-gconv-extra`: it is the
last release that carries those gconv modules in libc, and there is no such
package for it.

```bash
sudo dnf install epel-release
sudo dnf install gcc-c++ cmake git ncurses-devel zlib-devel \
                 expected-devel doctest-devel gettext
```

**Debian, Ubuntu** — on Ubuntu `libexpected-dev` is in universe:

```bash
sudo apt install g++ cmake git libncurses-dev zlib1g-dev \
                 libexpected-dev doctest-dev gettext
```

**Arch**

```bash
sudo pacman -S --needed base-devel cmake git ncurses zlib tl-expected doctest \
                        gettext
```

Then, on any of them:

```bash
git clone https://github.com/jegornet/amberedit && cd amberedit

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build          # optional
sudo cmake --install build      # /usr/local/bin/amberedit
```

Add `-DCMAKE_INSTALL_PREFIX=~/.local` to install without root.

**`glibc-gconv-extra`** is above for a reason: CP866, CP437 and KOI8-R are not
in the base glibc on RHEL 9 and later or on Fedora — the gconv modules for them
were split into that package. Without it `iconv_open("CP866")` fails, every
legacy-encoded message reads as mojibake, and three of the tests fail. The RPM
depends on it; a build from source has no way to ask. Debian, Ubuntu, Arch and
macOS all carry the full set.

**Without doctest**, build with `-DAMBEREDIT_BUILD_TESTS=OFF`; nothing but the
tests needs it, and on a small VPS they are by a wide margin the heaviest thing
in the build.

**The release carries the sources as a package too**, `amberedit-<version>-1.src.rpm`
— the spec file and that exact tarball, and nothing else in it. There is no
distribution in its name because there is none in the package: the spec inside keeps
`%{?dist}` unexpanded, so the rebuild stamps itself with wherever it is run.

```bash
rpmbuild --rebuild amberedit-<version>.src.rpm
```

That builds for the system and the architecture at hand — aarch64, which the release
has no binary for, or a distribution it does not cover at all.

## macOS

The ncurses Apple ships is 5.7 and has no wide characters at all, so a newer one
is needed:

```bash
brew install ncurses tl-expected doctest gettext
cmake -S . -B build \
      -DCMAKE_PREFIX_PATH="$(brew --prefix ncurses);$(brew --prefix tl-expected);$(brew --prefix doctest)"
```

Homebrew keeps gettext keg-only, which matters twice. Its `msgfmt` is not on the
PATH — the build looks in `$(brew --prefix)/opt/gettext/bin` and needs no help —
and its `libintl` is not where CMake looks by default, so the prefix has to be
named alongside the others:

```bash
cmake -S . -B build \
      -DCMAKE_PREFIX_PATH="$(brew --prefix ncurses);$(brew --prefix tl-expected);$(brew --prefix doctest);$(brew --prefix gettext)"
```

Unlike on Linux, `libintl` is then a library the finished binary links, not only
a build-time tool.

## Windows

**Windows 10 or later**, on x86_64. The release zip needs nothing installed:
unpack it and run `bin\amberedit.exe`. **Keep `share\` beside `bin\`** — the
message template, the themes and the message catalogs are looked for relative to
the executable, so moving the .exe out on its own leaves it without them.

### Building it there

MSYS2, in the **UCRT64** environment (not MINGW64).

```
pacman -S --needed git make \
    mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-zlib \
    mingw-w64-ucrt-x86_64-libiconv mingw-w64-ucrt-x86_64-gettext \
    mingw-w64-ucrt-x86_64-tl-expected mingw-w64-ucrt-x86_64-doctest

tools/build-pdcurses.sh "$PWD/w64deps"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$PWD/w64deps"
cmake --build build
ctest --test-dir build --output-on-failure
```

The curses is [PDCursesMod](https://github.com/Bill-Gray/PDCursesMod) rather than
ncurses, and it is the one dependency not taken from pacman: MSYS2 carries the
original PDCurses, which has neither the extended colour pairs nor the combining
characters AmberEdit asks for, and the probe in `CMakeLists.txt` refuses it.
`tools/build-pdcurses.sh` builds the right one — wide, 64-bit `chtype`, and
`PDC_RGB` so that the first eight colours are in the ANSI order the themes are
written in rather than the DOS order PDCurses defaults to.

### Cross-building it from Linux or macOS

Which is how the releases are made, and what `cmake/toolchain-mingw-w64.cmake` is
for. `tools/build-w64-deps.sh` builds zlib, libiconv, libintl and PDCursesMod for
the target into a prefix of their own — nothing is written into the source tree —
and the header-only tl::expected and doctest come from the host's own copies:

```bash
brew install mingw-w64          # or the distribution's mingw-w64 packages
tools/build-w64-deps.sh
cmake -S . -B build-w64 -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$HOME/w64deps/prefix;$(brew --prefix tl-expected);$(brew --prefix doctest)"
cmake --build build-w64
```

The tests build too, but nothing on the build host can run them: `ctest` there
registers one test standing for the whole binary, to be run under Wine or on
Windows. Their fixtures live in the source tree, so a suite built here and run
elsewhere is told where they were copied to with
`-DAMBEREDIT_TESTDATA_ROOT=C:/somewhere`.

## Running it

Run `amberedit --setup`, which asks what a first config should say and writes
one — or copy `amberedit.cfg.example` and fix up the paths in it yourself.
[README.md](README.md) has the rest: the config search order and what every
screen does, and [KEYS.md](KEYS.md) is the keyboard.
