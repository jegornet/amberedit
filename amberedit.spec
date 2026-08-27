# RPM packaging for RHEL 8 and 9 and their rebuilds (Rocky, AlmaLinux, CentOS
# Stream), and for Fedora.
#
# Everything comes from BaseOS and AppStream except the test framework, which is
# in EPEL, so `dnf install epel-release` first — or build with
#
#   rpmbuild -ba amberedit.spec --without check
#
# which drops both the dependency and the test run.

%bcond_without check

Name:           amberedit
Version:        0.5
Release:        1%{?dist}
Summary:        FidoNet mail editor

License:        GPL-2.0-or-later
URL:            https://github.com/jegornet/amberedit

Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

# GCC 8 is the floor and is what RHEL 8 ships: the code is C++17 and needs no
# gcc-toolset. CMake 3.16 likewise — RHEL 8 has 3.20 or newer.
BuildRequires:  gcc-c++ >= 8
BuildRequires:  cmake >= 3.16
BuildRequires:  make
BuildRequires:  pkgconfig
# The wide-character ncurses. ncurses-devel carries both the narrow and the
# wide library; the build probes for cchar_t and init_extended_pair and fails
# the configure if it got the narrow one.
BuildRequires:  pkgconfig(ncursesw)
# zlib to unpack zipped nodelists and echolists. Asked for by what it provides
# rather than by name: RHEL 10 and current Fedora replaced zlib with zlib-ng, so
# the package carrying zlib.h and zlib.pc is zlib-ng-compat-devel there and
# zlib-devel on RHEL 8 and 9. `pkgconfig(zlib)` is the one spelling that finds
# whichever of the two the distribution has.
BuildRequires:  pkgconfig(zlib)
# tl::expected, which every fallible operation in AmberEdit answers with.
# Header-only, so it is wanted at build time and never at run time. From EPEL on
# RHEL, as doctest is — std::expected would need C++23 and the floor is GCC 8.
BuildRequires:  expected-devel
# msgfmt, which compiles po/*.po into the catalogs the interface is drawn from,
# and %%find_lang below, which places them. Build time only on this platform:
# gettext's runtime lives in glibc here, so there is nothing extra to require.
BuildRequires:  gettext

# The single-byte charsets FidoNet runs on — CP866, CP437, KOI8-R — are not in
# the base glibc on RHEL 9 and later or on Fedora: the gconv modules for them
# were split out into glibc-gconv-extra, and RHEL 8 is the last release carrying
# them in libc. Nothing detects this automatically, because a gconv module is
# opened by name at run time and leaves no linkage for rpm to find. Without it
# iconv_open("CP866") fails and a CP866 message shows as mojibake — which is the
# whole of what this program is for. It is wanted at build time as well, since
# %%check converts between these charsets.
%if 0%{?rhel} >= 9 || 0%{?fedora}
BuildRequires:  glibc-gconv-extra
Requires:       glibc-gconv-extra
%endif
%if %{with check}
# Header-only, so it is wanted at build time and never at run time. One name on
# every release this builds on, EPEL 8 included — which is the whole reason the
# tests are written against doctest and not Catch2, whose 2.13 and v3 series are
# not source-compatible and are split across exactly these distributions.
BuildRequires:  doctest-devel
%endif

%description
A FidoNet mail editor for the terminal, reading Squish, JAM and Fido *.msg
message bases. Message areas can be read from a fidoconfig, areas.bbs or
squish.cfg. Supports both UTF-8 and legacy encodings such as CP866 or CP437.

%prep
%setup -q

%build
# AMBEREDIT_BUILD_TESTS is stated either way: with --without check there is no
# doctest installed to find, and the default would stop the configure.
%cmake -DAMBEREDIT_BUILD_TESTS:BOOL=%{?with_check:ON}%{!?with_check:OFF}
%cmake_build

%if %{with check}
%check
%ctest
%endif

%install
# The binary, the template, the themes and the message catalogs all come from the
# CMake install, which places them where amberedit.cfg.example says they are.
# Nothing is placed here by hand: a second copy of those paths is a second thing
# to keep in step.
%cmake_install

# The catalogs go under %%{_datadir}/locale, which is where gettext looks and so
# the only place they can go. %%find_lang is what turns them into per-language
# %%lang() entries, and it is why the %%files list below names no locale at all.
%find_lang %{name}

%files -f %{name}.lang
%license LICENSE
# The sample config stays documentation: AmberEdit looks for its config where
# the user keeps it, and this one is here to be copied.
%doc README.md INSTALL.md amberedit.cfg.example amberkeys.cfg.example
%{_bindir}/amberedit
%dir %{_datadir}/%{name}
%{_datadir}/%{name}/default.tpl
%{_datadir}/%{name}/themes

%changelog
* Fri Aug 28 2026 Yegor Gluhov <git@jegor.net> - 0.5-1
- Marking messages and group actions
- Go to message by number
- dialog_tall_buttons config option
- Minor fixes

* Thu Aug 27 2026 Yegor Gluhov <git@jegor.net> - 0.4.5-1
- External editor
- Nodelist encoding
- Minor fixes

* Thu Aug 27 2026 Yegor Gluhov <git@jegor.net> - 0.4.4-1
- Reader: sidebar position setting
- External utilities
- keys_mode option: merge a keys file onto the defaults

* Wed Aug 26 2026 Yegor Gluhov <git@jegor.net> - 0.4.3-1
- Ignore the wheel's tail after a change

* Tue Aug 25 2026 Yegor Gluhov <git@jegor.net> - 0.4.2-1
- Reader: go to message by number

* Tue Aug 25 2026 Yegor Gluhov <git@jegor.net> - 0.4.1-1
- Run shell

* Tue Aug 25 2026 Yegor Gluhov <git@jegor.net> - 0.4-1
- Add Russian localization
- Override Home, End keys ESC sequences

* Mon Aug 24 2026 Yegor Gluhov <git@jegor.net> - 0.3.2-1
- Message list side bar
- RHEL 10 build

* Sun Aug 23 2026 Yegor Gluhov <git@jegor.net> - 0.3.1-1
- Add setup wizard
- Specify datetime format in msglist_format

* Sun Aug 23 2026 Yegor Gluhov <git@jegor.net> - 0.3-1
- Make hint bar customizable

* Sun Aug 23 2026 Yegor Gluhov <git@jegor.net> - 0.2-1
- Fix ANSI graphics

* Sun Aug 23 2026 Yegor Gluhov <git@jegor.net> - 0.1.8-1
- ANSI graphics support (experimental)

* Sat Aug 22 2026 Yegor Gluhov <git@jegor.net> - 0.1.7-1
- Add comment reply

* Sat Aug 22 2026 Yegor Gluhov <git@jegor.net> - 0.1.6-1
- Add error log

* Sat Aug 22 2026 Yegor Gluhov <git@jegor.net> - 0.1.5-1
- Improve themes

* Fri Aug 21 2026 Yegor Gluhov <git@jegor.net> - 0.1.4-1
- Add dialog colors
- Throttle mouse wheel events

* Fri Aug 21 2026 Yegor Gluhov <git@jegor.net> - 0.1.3-1
- Fix quote blank lines

* Fri Aug 21 2026 Yegor Gluhov <git@jegor.net> - 0.1.2-1
- Fix arealist foreground color

* Thu Aug 20 2026 Yegor Gluhov <git@jegor.net> - 0.1.1-1
- Initial package.
