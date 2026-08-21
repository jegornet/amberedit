# Installing AmberEdit

Every tagged release carries built packages —
[Releases](https://github.com/jegornet/amberedit/releases) has RPMs for RHEL 8, 9
and Fedora, debs for Debian stable and Ubuntu 22.04 and 24.04, and tarballs for
macOS on both architectures. Building it yourself is the rest of this file.

## What it needs

CMake ≥ 3.16, a C++17 compiler (GCC 8, Clang 7, Apple Clang 11), git, iconv (in
libc or libiconv), zlib and the wide-character ncurses. Catch2 2.13 is used if it
is installed and fetched during the build if it is not. There are no other
dependencies.

That floor is chosen so that RHEL 8 and its rebuilds build AmberEdit out of the
box with nothing but the stock toolchain — no devtoolset, no newer CMake.

## Building

```bash
sudo dnf install gcc-c++ cmake git ncurses-devel zlib-devel        # RHEL 8 / Rocky / Alma
sudo apt install g++ cmake git libncurses-dev zlib1g-dev           # Debian / Ubuntu

git clone https://github.com/jegornet/amberedit && cd amberedit

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build          # optional
sudo cmake --install build      # /usr/local/bin/amberedit
```

Add `-DCMAKE_INSTALL_PREFIX=~/.local` to install without root.

**On RHEL 9 and later, and on Fedora, add `glibc-gconv-extra`:**

```bash
sudo dnf install glibc-gconv-extra
```

CP866, CP437 and KOI8-R are not in the base glibc there — the gconv modules for
them were split into that package, RHEL 8 being the last release that carries
them in libc. Without it `iconv_open("CP866")` fails, every legacy-encoded
message reads as mojibake, and three of the tests fail. The RPM depends on it; a
build from source has no way to ask. Debian, Ubuntu and macOS all carry the full
set.

**On a small VPS**, build with `-DAMBEREDIT_BUILD_TESTS=OFF`. Catch2 is by a wide
margin the heaviest thing in the build, and nothing but the tests needs it.

**The release carries the sources as a package too**, `amberedit-<version>-1.src.rpm`
— the spec file and that exact tarball, and nothing else in it. There is no
distribution in its name because there is none in the package: the spec inside keeps
`%{?dist}` unexpanded, so the rebuild stamps itself with wherever it is run.

```bash
rpmbuild --rebuild amberedit-0.1.1-1.src.rpm
```

That builds for the system and the architecture at hand — aarch64, which the release
has no binary for, or a distribution it does not cover at all.

## macOS

The ncurses Apple ships is 5.7 and has no wide characters at all, so a newer one
is needed:

```bash
brew install ncurses
cmake -S . -B build -DCMAKE_PREFIX_PATH=$(brew --prefix ncurses)
```

## Running it

Copy `amberedit.cfg.example`, fix up the paths in it, and run `amberedit` —
[README.md](README.md) has the rest: the config search order, the keys, and what
every screen does.
