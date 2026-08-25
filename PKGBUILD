# Arch packaging. Everything the build needs is in core and extra — ncurses,
# zlib, cmake, and the two header-only ones, tl-expected and doctest — so this
# builds with pacman alone and nothing from the AUR.
#
# Maintainer: Yegor Gluhov <git@jegor.net>

pkgname=amberedit
pkgver=0.3.2
pkgrel=1
pkgdesc='FidoNet mail editor for the terminal'
arch=('x86_64')
url='https://github.com/jegornet/amberedit'
license=('GPL-2.0-or-later')
# No glibc-gconv-extra counterpart, because Arch has no such split: CP866,
# CP437 and KOI8-R are in the base glibc here, which is what check() proves.
depends=('glibc' 'ncurses' 'zlib')
# tl::expected and doctest are header-only, so both are wanted while building
# and neither at run time.
# gettext for msgfmt, which compiles po/*.po into the catalogs the interface is
# drawn from — build time only, since nothing here links libintl.
makedepends=('cmake' 'tl-expected' 'doctest' 'gettext')
source=("$pkgname-$pkgver.tar.gz::$url/archive/v$pkgver/$pkgname-$pkgver.tar.gz")
# GitHub generates its source archives on demand and they are not byte-stable
# across git versions, so a pinned hash is a package that stops building for a
# reason that has nothing to do with the release. The release workflow hands
# makepkg the same tarball the RPMs are built from, already in place beside
# this file, so nothing is fetched there at all.
sha256sums=('SKIP')

build() {
    # CMAKE_BUILD_TYPE=None is the Arch convention: it leaves makepkg.conf's
    # CFLAGS and LDFLAGS alone instead of overriding them with -O3 -DNDEBUG.
    cmake -S "$pkgname-$pkgver" -B build -DCMAKE_BUILD_TYPE=None \
          -DCMAKE_INSTALL_PREFIX=/usr -Wno-dev
    cmake --build build
}

check() {
    ctest --test-dir build --output-on-failure
}

package() {
    # The binary, the template and the themes all come from the CMake install,
    # which places them where amberedit.cfg.example says they are. Only the
    # documentation is placed here.
    DESTDIR="$pkgdir" cmake --install build

    cd "$pkgname-$pkgver"
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
    # The sample configs stay documentation: AmberEdit looks for its config
    # where the user keeps it, and these are here to be copied.
    install -Dm644 -t "$pkgdir/usr/share/doc/$pkgname" \
            README.md INSTALL.md amberedit.cfg.example amberkeys.cfg.example
}
