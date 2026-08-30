cmake_minimum_required(VERSION 3.16)
project(AmberEdit VERSION 0.6.1 LANGUAGES CXX C)

# C++17, and deliberately not newer: the oldest system AmberEdit is meant to build
# on out of the box is RHEL 8, whose stock toolchain is GCC 8.5 and CMake 3.20.
# (8.8 and later ship 3.26; 3.20 is the older of the two and so the floor.)
# That is the whole of the constraint — nothing here wants a later standard.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- compiler floor ----------------------------------------------------------
# CMAKE_CXX_STANDARD above does not keep an old compiler out: GCC 6 accepts
# -std=c++17, so CMake counts the standard as satisfied, configures cleanly and
# the build then falls over deep inside <bits/> over string_view and structured
# bindings. GCC 8 is the first release with C++17 complete enough to build this
# (and is what RHEL 8 ships); Clang 7 and Apple Clang 11 are the equivalents.
# An unrecognised compiler is left alone rather than guessed at.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(amberedit_min_compiler 8)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
  set(amberedit_min_compiler 11)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(amberedit_min_compiler 7)
endif()

if(amberedit_min_compiler AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS amberedit_min_compiler)
  message(FATAL_ERROR
    "AmberEdit is C++17 and needs ${CMAKE_CXX_COMPILER_ID} ${amberedit_min_compiler} or newer; "
    "found ${CMAKE_CXX_COMPILER_VERSION} at ${CMAKE_CXX_COMPILER}."
  )
endif()

option(AMBEREDIT_BUILD_TESTS "Build AmberEdit unit tests" ON)

# Up here rather than beside the install() rules, because the message catalogs
# are found through a path compiled into the binary and that path is
# ${CMAKE_INSTALL_FULL_LOCALEDIR} — which is empty until this has run.
include(GNUInstallDirs)

# --- ncursesw ----------------------------------------------------------------
# The wide-character ncurses, which every distribution ships and none of them
# agrees on the spelling of. Three things have to be established, and none can be
# taken on trust:
#
#   * which library to link. pkg-config knows on Linux; on macOS it answers for
#     `ncursesw` with the plain `-lncurses` of the ncurses 5.7 Apple still
#     ships, which has no wide characters at all.
#   * which header to include. `<ncurses.h>` finds Apple's even with Homebrew's
#     include directory ahead of it, so where a `ncursesw/` directory exists it
#     is asked for by name — that path is unambiguous everywhere it exists.
#   * whether what was found actually has the wide and extended-color API.
#     That is settled by compiling, below, because it is the only answer that
#     cannot be wrong.
include(CheckCXXSourceCompiles)
include(CMakePushCheckState)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(NCURSESW QUIET IMPORTED_TARGET ncursesw)
endif()

if(TARGET PkgConfig::NCURSESW)
  set(amberedit_curses_lib PkgConfig::NCURSESW)
else()
  set(CURSES_NEED_NCURSES TRUE)
  set(CURSES_NEED_WIDE TRUE)
  find_package(Curses REQUIRED)
  add_library(amberedit_curses INTERFACE)
  target_link_libraries(amberedit_curses INTERFACE ${CURSES_LIBRARIES})
  target_include_directories(amberedit_curses INTERFACE ${CURSES_INCLUDE_DIRS})
  set(amberedit_curses_lib amberedit_curses)
endif()

# Does the header live under ncursesw/, and does what we found do wide
# characters and extended color pairs? One compile answers all three.
set(amberedit_curses_probe "
  #include <@header@>
  int main() {
    cchar_t cell; wchar_t text[2] = {L'x', 0}; int pair = 1;
    setcchar(&cell, text, A_BOLD, 0, &pair);
    init_extended_pair(1, 2, 3);
    return tigetflag((char*)\"RGB\");
  }
")

cmake_push_check_state()
set(CMAKE_REQUIRED_LIBRARIES ${amberedit_curses_lib})
set(CMAKE_REQUIRED_DEFINITIONS -DNCURSES_WIDECHAR=1)

string(REPLACE "@header@" "ncursesw/curses.h" amberedit_probe_subdir "${amberedit_curses_probe}")
check_cxx_source_compiles("${amberedit_probe_subdir}" AMBEREDIT_NCURSESW_SUBDIR)

if(NOT AMBEREDIT_NCURSESW_SUBDIR)
  string(REPLACE "@header@" "curses.h" amberedit_probe_plain "${amberedit_curses_probe}")
  check_cxx_source_compiles("${amberedit_probe_plain}" AMBEREDIT_CURSES_PLAIN)
  if(NOT AMBEREDIT_CURSES_PLAIN)
    message(FATAL_ERROR
      "AmberEdit needs the wide-character ncurses (cchar_t, setcchar, "
      "init_extended_pair). Install ncurses-devel / libncursesw5-dev; on macOS "
      "the system ncurses is 5.7 and too old, so `brew install ncurses` and "
      "configure with -DCMAKE_PREFIX_PATH=$(brew --prefix ncurses)."
    )
  endif()
endif()
cmake_pop_check_state()

# --- iconv -------------------------------------------------------------------
# On glibc iconv lives in libc; on macOS/BSD it is a separate libiconv.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
  #include <iconv.h>
  int main() { iconv_t c = iconv_open(\"UTF-8\", \"CP866\"); iconv_close(c); }
" ICONV_IN_LIBC)

if(NOT ICONV_IN_LIBC)
  find_library(ICONV_LIBRARY NAMES iconv libiconv REQUIRED)
  find_path(ICONV_INCLUDE_DIR NAMES iconv.h REQUIRED)
endif()

add_library(amberedit_iconv INTERFACE)
if(NOT ICONV_IN_LIBC)
  target_link_libraries(amberedit_iconv INTERFACE ${ICONV_LIBRARY})
  target_include_directories(amberedit_iconv INTERFACE ${ICONV_INCLUDE_DIR})
endif()

# --- libintl -----------------------------------------------------------------
# gettext's runtime, which is what draws the interface in a language other than
# the English it is written in. On glibc it is part of libc and there is nothing
# to link; on macOS and the BSDs it is a library of its own, and Homebrew's is
# keg-only, so the prefix has to be named. CMake's own FindIntl settles which of
# the two this is — by variables and not by the `Intl::Intl` target, which is
# CMake 3.20 and the floor here is 3.16.
# Not REQUIRED: its stock "Could NOT find Intl" says nothing about which package
# to install, and on macOS the answer is not obvious — the library is there but
# Homebrew keeps gettext keg-only, so the prefix has to be handed to CMake.
find_package(Intl QUIET)

if(NOT Intl_FOUND)
  message(FATAL_ERROR
    "AmberEdit needs gettext's runtime (<libintl.h>), which is what draws the "
    "interface in a language other than English. On glibc it is part of libc and "
    "should have been found; install glibc-devel / libc6-dev if it was not. On "
    "macOS it is `brew install gettext`, and because Homebrew keeps that keg-only "
    "the prefix has to be named: -DCMAKE_PREFIX_PATH=$(brew --prefix gettext)."
  )
endif()

add_library(amberedit_intl INTERFACE)
if(Intl_LIBRARIES)
  target_link_libraries(amberedit_intl INTERFACE ${Intl_LIBRARIES})
endif()
if(Intl_INCLUDE_DIRS)
  target_include_directories(amberedit_intl INTERFACE ${Intl_INCLUDE_DIRS})
endif()

# `_nl_msg_cat_cntr` is how a program that changes `LANGUAGE` while it runs tells
# gettext to look again — without it the first answer is cached for the life of
# the process. glibc and GNU libintl both export it and gettext's own manual is
# where it is documented, but it is in no header, so it is declared by hand and
# probed for rather than assumed: a C library that has gettext and not this
# should still build, and simply not be able to put a catalog back down.
cmake_push_check_state()
set(CMAKE_REQUIRED_LIBRARIES ${Intl_LIBRARIES})
set(CMAKE_REQUIRED_INCLUDES ${Intl_INCLUDE_DIRS})
check_cxx_source_compiles("
  #include <libintl.h>
  extern \"C\" { extern int _nl_msg_cat_cntr; }
  int main() { ++_nl_msg_cat_cntr; return gettext(\"\")[0]; }
" AMBEREDIT_HAVE_NL_MSG_CAT_CNTR)
cmake_pop_check_state()

# --- zlib --------------------------------------------------------------------
# A zone's pointlist arrives as a zip, and unpacking one is deflate and a CRC —
# both of which zlib is, and neither of which is worth a second copy of. It is
# on every system that has a package manager, so it is found and not fetched:
# pkg-config first, as ncursesw is, and CMake's own FindZLIB where there is no
# zlib.pc to answer.
if(PkgConfig_FOUND)
  pkg_check_modules(ZLIB QUIET IMPORTED_TARGET zlib)
endif()

if(TARGET PkgConfig::ZLIB)
  set(amberedit_zlib_lib PkgConfig::ZLIB)
else()
  find_package(ZLIB REQUIRED)
  set(amberedit_zlib_lib ZLIB::ZLIB)
endif()

# --- tl::expected ------------------------------------------------------------
# Every fallible operation in AmberEdit answers with tl::expected<T, ErrorPtr>,
# so this is not optional and no part of the code has a path around its absence.
# std::expected does the same job and is C++23; the floor is RHEL 8 and GCC 8, so
# it is not on the table and will not be.
#
# Found and never fetched, like everything else: expected-devel on RHEL and
# Fedora, from EPEL on RHEL as doctest is; libexpected-dev on Debian and Ubuntu,
# in universe on the two Ubuntu LTS releases; tl-expected in Homebrew. All of
# them install <tl/expected.hpp> and a CMake package called tl-expected — but
# jammy carries 1.0.0, whose CMake package predates the rewrite of it in 1.1.0
# and does not always export the target the later ones do. Debian's floor is
# trixie, which is past that. So both are asked, and a plain search for the
# header answers where neither does.
if(PkgConfig_FOUND)
  pkg_check_modules(TL_EXPECTED QUIET IMPORTED_TARGET tl-expected)
endif()

if(NOT TL_EXPECTED_FOUND)
  find_package(tl-expected QUIET CONFIG)
endif()

# What a header-only library comes down to is the directory its header is in,
# and having it as a directory rather than a target is also what lets the probe
# below run: a try_compile is a project of its own and cannot see a target this
# one defines.
set(amberedit_expected_includes "")
if(TL_EXPECTED_FOUND)
  set(amberedit_expected_includes ${TL_EXPECTED_INCLUDE_DIRS})
elseif(TARGET tl::expected)
  get_target_property(amberedit_expected_includes tl::expected
                      INTERFACE_INCLUDE_DIRECTORIES)
endif()

if(NOT amberedit_expected_includes)
  find_path(TL_EXPECTED_INCLUDE_DIR NAMES tl/expected.hpp)
  set(amberedit_expected_includes ${TL_EXPECTED_INCLUDE_DIR})
endif()

add_library(amberedit_expected INTERFACE)
if(amberedit_expected_includes)
  target_include_directories(amberedit_expected
                             INTERFACE ${amberedit_expected_includes})
endif()

# Only the 1.0.0 API is exercised, here and in the code: no transform, no
# transform_error, and tl::unexpected written out with make_unexpected rather
# than deduced. jammy carries 1.0.0 and has to build; every other target is
# 1.1.0 or later.
#
# The second half of the probe is the one worth having. The error type is
# `ErrorPtr` — a `std::unique_ptr` — so tl::expected has to hold a type it can
# move and cannot copy, and has to let `std::move(r).error()` hand the pointer
# out of an expected that is about to die. That is the whole of how an error
# travels here, and a version that cannot do it has to fail at configure time
# and say so, rather than a thousand lines into the tree.
set(amberedit_expected_probe "
  #include <memory>
  #include <string>
  #include <tl/expected.hpp>
  tl::expected<int, std::string> ok() { return 42; }
  tl::expected<void, std::string> bad() {
    return tl::make_unexpected(std::string(\"no\"));
  }
  using Moved = std::unique_ptr<const std::string>;
  tl::expected<int, Moved> deep() {
    return tl::make_unexpected(Moved(new std::string(\"no\")));
  }
  tl::expected<void, Moved> deeper() {
    auto read = deep();
    if (!read) return tl::make_unexpected(std::move(read).error());
    return {};
  }
  int main() {
    if (!ok().has_value() || bad().has_value()) return 1;
    if (ok().value_or(0) != 42 || bad().error() != \"no\") return 1;
    const auto moved = deeper();
    if (moved.has_value() || *moved.error() != \"no\") return 1;
    return 0;
  }
")

cmake_push_check_state()
set(CMAKE_REQUIRED_INCLUDES ${amberedit_expected_includes})
check_cxx_source_compiles("${amberedit_expected_probe}" AMBEREDIT_HAVE_TL_EXPECTED)
cmake_pop_check_state()

if(NOT AMBEREDIT_HAVE_TL_EXPECTED)
  message(FATAL_ERROR
    "AmberEdit needs tl::expected (<tl/expected.hpp>), and one that can hold a "
    "move-only error. Install expected-devel on RHEL and Fedora, from EPEL on "
    "RHEL; libexpected-dev on Debian and Ubuntu; `brew install tl-expected` on "
    "macOS, where it also wants "
    "-DCMAKE_PREFIX_PATH=$(brew --prefix tl-expected). A header that is found "
    "and still fails this probe is too old for a move-only error type."
  )
endif()

# --- std::filesystem ---------------------------------------------------------
# GCC 8 — RHEL 8's compiler — ships std::filesystem in a separate libstdc++fs
# that has to be named on the link line; the header compiles either way, so the
# symptom without it is a page of undefined references at link time. GCC 9 and
# later, and libc++, fold it into the standard library. Which case applies is
# settled by linking rather than by version arithmetic.
set(amberedit_fs_probe "
  #include <filesystem>
  int main() {
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(\"/\"), ec) ? 0 : 1;
  }
")
check_cxx_source_compiles("${amberedit_fs_probe}" AMBEREDIT_FILESYSTEM_IN_LIBSTDCXX)

if(NOT AMBEREDIT_FILESYSTEM_IN_LIBSTDCXX)
  cmake_push_check_state()
  set(CMAKE_REQUIRED_LIBRARIES stdc++fs)
  check_cxx_source_compiles("${amberedit_fs_probe}" AMBEREDIT_FILESYSTEM_NEEDS_STDCXXFS)
  cmake_pop_check_state()
  if(NOT AMBEREDIT_FILESYSTEM_NEEDS_STDCXXFS)
    message(FATAL_ERROR
      "AmberEdit needs a working std::filesystem. ${CMAKE_CXX_COMPILER_ID} "
      "${CMAKE_CXX_COMPILER_VERSION} provides neither <filesystem> in the "
      "standard library nor a linkable libstdc++fs."
    )
  endif()
endif()

add_library(amberedit_filesystem INTERFACE)
if(AMBEREDIT_FILESYSTEM_NEEDS_STDCXXFS)
  target_link_libraries(amberedit_filesystem INTERFACE stdc++fs)
endif()

# --- embedded resources ------------------------------------------------------
# `--setup` writes a whole commented config, which means it needs the sample one,
# and the sample one is not installed (see the install() rules at the end: the
# binary, the template and the themes go in, the documentation does not). So it
# is compiled in, and `default.tpl` with it for a `--setup` run that finds no
# installed copy of the template to point the config at.
#
# The generated source is part of the core rather than of the executable so that
# the tests reach it: what the wizard writes is checked by parsing it back, and
# that check is worth nothing if it does not run against the file that ships.
set(amberedit_embedded ${CMAKE_BINARY_DIR}/generated/embedded_resources.cpp)
add_custom_command(
  OUTPUT ${amberedit_embedded}
  COMMAND ${CMAKE_COMMAND}
          -DEXAMPLE_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/amberedit.cfg.example
          -DDEFAULT_TEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/default.tpl
          -DOUTPUT=${amberedit_embedded}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_resources.cmake
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/amberedit.cfg.example
          ${CMAKE_CURRENT_SOURCE_DIR}/default.tpl
          ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_resources.cmake
  COMMENT "Embedding amberedit.cfg.example and default.tpl"
  VERBATIM)

# --- application core --------------------------------------------------------
add_library(amberedit_core STATIC
  src/support/error.cpp
  src/i18n/i18n.cpp
  src/domain/ftn_address.cpp
  src/domain/address_pattern.cpp
  src/domain/area.cpp
  src/domain/message.cpp
  src/config/text_util.cpp
  src/config/commands.cpp
  src/config/temp_dir.cpp
  src/config/cfg_file.cpp
  src/config/list_format.cpp
  src/config/area_pattern.cpp
  src/config/app_config.cpp
  src/config/fidoconfig_parser.cpp
  src/config/manual_area_source.cpp
  src/config/areas_bbs_parser.cpp
  src/config/squish_cfg_parser.cpp
  src/config/config_writer.cpp
  ${amberedit_embedded}
  src/encoding/charset_detector.cpp
  src/encoding/iconv_recoder.cpp
  src/encoding/locale_charset.cpp
  src/encoding/text_search.cpp
  src/msgbase/binary_file.cpp
  src/msgbase/file_lock.cpp
  src/msgbase/jam_crc32.cpp
  src/msgbase/info_format.cpp
  src/msgbase/raw_message.cpp
  src/msgbase/squish_base.cpp
  src/msgbase/jam_base.cpp
  src/msgbase/sdm_base.cpp
  src/msgbase/ftn_msgbase.cpp
  src/msgbase/lastread_file.cpp
  src/msgbase/squish_lastread_store.cpp
  src/msgbase/jam_lastread_store.cpp
  src/msgbase/fido_lastread_store.cpp
  src/msgbase/msgbase_lastread_store.cpp
  src/app/area_manager.cpp
  src/app/compose_prefill.cpp
  src/app/export_file.cpp
  src/app/import_file.cpp
  src/app/quoting.cpp
  src/app/copy_commands.cpp
  src/app/msg_template.cpp
  src/app/message_builder.cpp
  src/app/message_search.cpp
  src/app/user_shell.cpp
  src/app/url_handler.cpp
  src/app/external_editor.cpp
  src/app/run_program.cpp
  src/app/navigator.cpp
)
# The version reaches the code through a generated header, so that
# project(VERSION) above stays the one place it is written down. The system is
# taken at configure time rather than from the preprocessor: it is the system
# AmberEdit is built for, which is what a message signed "AmberEdit/linux" claims.
string(TOLOWER "${CMAKE_SYSTEM_NAME}" AMBEREDIT_SYSTEM)
configure_file(src/version.hpp.in ${CMAKE_BINARY_DIR}/generated/version.hpp @ONLY)

target_include_directories(amberedit_core PUBLIC src ${CMAKE_BINARY_DIR}/generated)
if(AMBEREDIT_HAVE_NL_MSG_CAT_CNTR)
  target_compile_definitions(amberedit_core PRIVATE AMBEREDIT_HAVE_NL_MSG_CAT_CNTR)
endif()

# Where the message catalogs are, compiled in because `bindtextdomain()` has to
# be told and there is nothing at run time that could work it out.
#
# Two of them, and the build's own comes first. An installed AmberEdit finds its
# catalogs under the install prefix and needs nothing said anywhere; one that has
# only been built finds them in its own build directory, so `LANG=ru_RU.UTF-8
# ./build/bin/amberedit` is Russian straight out of `cmake --build`. The build
# path simply does not exist on any machine the binary is shipped to, so the
# first check falls through and the installed one answers.
target_compile_definitions(amberedit_core PRIVATE
  AMBEREDIT_LOCALEDIR="${CMAKE_INSTALL_FULL_LOCALEDIR}"
  AMBEREDIT_BUILD_LOCALEDIR="${CMAKE_BINARY_DIR}/locale"
)
target_link_libraries(amberedit_core
  PRIVATE amberedit_iconv
  # gettext's runtime, which `i18n/` is the whole of the use of. PUBLIC because
  # `i18n/i18n.hpp` is included all over the tree — every screen draws words —
  # and on macOS that header is Homebrew's rather than the system's.
  PUBLIC amberedit_intl
  # PUBLIC because everything linking the core — the executable and the tests —
  # uses std::filesystem too, and on GCC 8 that is an extra -l rather than free.
  PUBLIC amberedit_filesystem
  # PUBLIC because tl::expected stands in the core's own headers, so the nodelist,
  # the echolist, the zip reader, the UI and the tests all include it too.
  PUBLIC amberedit_expected
)

# --- zip archives ------------------------------------------------------------
# Enough zip to unpack what arrives in one — a zone's pointlist and an echolist
# both do. Its own library because it is the whole of what wants zlib, and
# because the two subsystems that read archives must not have to reach through
# each other to get at it.
add_library(amberedit_zip STATIC
  src/archive/zip_reader.cpp
)
target_include_directories(amberedit_zip PUBLIC src)
target_link_libraries(amberedit_zip
  # zlib is unpacking an archive and nothing else, so it stops here.
  PRIVATE ${amberedit_zlib_lib}
  # For msgbase/byte_order, which is where the little-endian reads live.
  PUBLIC amberedit_core
)

# --- the nodelist ------------------------------------------------------------
# Reading a nodelist, compiling it, and reading the compiled file back. Its own
# library rather than part of the core: it is a whole subsystem the reader
# reaches into at one key, and nothing in the core knows it is there.
add_library(amberedit_nodelist STATIC
  src/nodelist/node_entry.cpp
  src/nodelist/nodelist_format.cpp
  src/nodelist/nodelist_parser.cpp
  src/nodelist/nodelist_writer.cpp
  src/nodelist/nodelist_db.cpp
  src/nodelist/nodelist_source.cpp
  src/nodelist/nodelist_compiler.cpp
)
target_include_directories(amberedit_nodelist PUBLIC src)
target_link_libraries(amberedit_nodelist
  # A zipped pointlist is unpacked on the way in and nowhere else, so the zip
  # reader stops here.
  PRIVATE amberedit_zip
  # The address and the text helpers, which are the core's — nothing in the
  # core knows about the nodelist, so this leans one way only.
  PUBLIC amberedit_core
)

# --- the echolist ------------------------------------------------------------
# Reading echolists, compiling them, and laying the descriptions they carry over
# the area list. Beside the nodelist rather than inside it: the two share the
# shape of the work — a source that may arrive zipped, a compiled file with a
# state per source in it — and nothing else, and neither knows the other exists.
add_library(amberedit_echolist STATIC
  src/echolist/echolist_format.cpp
  src/echolist/echolist_parser.cpp
  src/echolist/echolist_writer.cpp
  src/echolist/echolist_db.cpp
  src/echolist/echolist_source.cpp
  src/echolist/echolist_compiler.cpp
  src/echolist/echolist_area_source.cpp
)
target_include_directories(amberedit_echolist PUBLIC src)
target_link_libraries(amberedit_echolist
  PRIVATE amberedit_zip
  PUBLIC amberedit_core
)

# --- UI and the executable ---------------------------------------------------
# The terminal layer: the only code that knows ncurses exists. Kept as its own
# library so that the tests can use the parts that draw and measure without
# linking a terminal they have no use for.
add_library(amberedit_term STATIC
  src/ui/term/color.cpp
  src/ui/term/element.cpp
  src/ui/term/event.cpp
  src/ui/term/screen.cpp
  src/ui/term/terminal.cpp
  src/ui/term/utf8.cpp
)
target_include_directories(amberedit_term PUBLIC src)
target_link_libraries(amberedit_term PUBLIC ${amberedit_curses_lib})
target_compile_definitions(amberedit_term PUBLIC NCURSES_WIDECHAR=1)
if(AMBEREDIT_NCURSESW_SUBDIR)
  target_compile_definitions(amberedit_term PUBLIC AMBEREDIT_NCURSESW_SUBDIR)
endif()

add_executable(amberedit
  src/main.cpp
  src/ui/app_shell.cpp
  src/ui/area_dialog.cpp
  src/ui/ansi_canvas.cpp
  src/ui/bbs_codes.cpp
  src/ui/area_list_format.cpp
  src/ui/msg_list_format.cpp
  src/ui/confirm_dialog.cpp
  src/ui/scope_dialog.cpp
  src/ui/dialog_frame.cpp
  src/ui/dir_listing.cpp
  src/ui/error_dialog.cpp
  src/ui/error_log.cpp
  src/ui/export_dialog.cpp
  src/ui/export_mode_dialog.cpp
  src/ui/external_dialog.cpp
  src/ui/find_dialog.cpp
  src/ui/attributes_dialog.cpp
  src/ui/forward_dialog.cpp
  src/ui/hint_bar.cpp
  src/ui/extern_util.cpp
  src/ui/after_handover.cpp
  src/ui/import_dialog.cpp
  src/ui/info_dialog.cpp
  src/ui/input_field.cpp
  src/ui/keys.cpp
  src/ui/mark_dialog.cpp
  src/ui/menu_dialog.cpp
  src/ui/message_marks.cpp
  src/ui/nodelist_dialog.cpp
  src/ui/reader_sidebar.cpp
  src/ui/reply_dialog.cpp
  src/ui/rescan_dialog.cpp
  src/ui/text_field.cpp
  src/ui/text_layout.cpp
  src/ui/text_editor.cpp
  src/ui/edit_layout.cpp
  src/ui/theme.cpp
  src/ui/setup/file_picker.cpp
  src/ui/setup/setup_run.cpp
  src/ui/setup/setup_wizard.cpp
  src/ui/setup/wizard_checks.cpp
  src/ui/screens/area_list_screen.cpp
  src/ui/screens/compose_screen.cpp
  src/ui/screens/message_list_screen.cpp
  src/ui/screens/message_read_screen.cpp
)
# The core comes with the nodelist library, which exports it; naming it here as
# well would put it on the link line twice.
target_link_libraries(amberedit PRIVATE amberedit_nodelist amberedit_echolist
                                        amberedit_term)

# What a build installs: the binary, the default template and the themes.
# Nothing looks for the last two beside the executable — a user points the config
# at one by path, and `amberedit.cfg.example` names them under
# ${CMAKE_INSTALL_DATADIR}/amberedit. So the install has to put them exactly
# there, or the sample config points at nothing. Placing them here rather than in
# each package's own recipe is what keeps the RPM, the deb and a plain
# `cmake --install` agreeing on where a theme lives.
#
# The sample configs, the README and INSTALL.md stay out: they are documentation,
# and each package format has its own place for that.
install(TARGETS amberedit RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES default.tpl DESTINATION ${CMAKE_INSTALL_DATADIR}/amberedit)
# The exclusion is not paranoia: `._blue.cfg` beside `blue.cfg` matches
# `*.cfg` and would be installed as a theme. macOS writes those sidecars whenever
# a file with extended attributes is carried through an archive, so a package
# built from a tarball made on a Mac ships them.
install(DIRECTORY themes/ DESTINATION ${CMAKE_INSTALL_DATADIR}/amberedit/themes
        FILES_MATCHING PATTERN "*.cfg" PATTERN "._*" EXCLUDE)

# --- message catalogs --------------------------------------------------------
# The interface's own language. `po/*.po` are the translations and `msgfmt` turns
# each into the `.mo` libintl reads.
#
# **The layout is gettext's own** — `<dir>/<language>/LC_MESSAGES/amberedit.mo` —
# and not a shape of ours, because `bindtextdomain()` is the only way in and that
# is the only shape it looks in. The config's `language` line names the whole
# path of one of those files and `i18n::load()` takes it apart again into the
# three things libintl wants.
#
# Built into the build directory under exactly the name it is installed under, so
# that a binary that has never been installed can be pointed at one: `language
# build/locale/ru/LC_MESSAGES/amberedit.mo` works out of a fresh checkout, which
# is the whole reason the setting takes a path rather than a language name.
#
# `msgfmt` is not required. A build without it is a build with the English the
# source is written in, which is every string the program has; the catalogs are
# what a package ships beside them, and `amberedit.spec`, `debian/control` and
# `PKGBUILD` all ask for gettext so that a package always has them.
# The Homebrew hints are for the same reason the ncurses block above has its
# own: gettext is keg-only there, so `brew install gettext` puts msgfmt in the
# cellar and not on the PATH. Both prefixes are named because Apple silicon and
# Intel use different ones, and neither exists anywhere else.
set(amberedit_gettext_hints /opt/homebrew/opt/gettext/bin /usr/local/opt/gettext/bin)
find_program(AMBEREDIT_MSGFMT msgfmt HINTS ${amberedit_gettext_hints})
find_program(AMBEREDIT_XGETTEXT xgettext HINTS ${amberedit_gettext_hints})
find_program(AMBEREDIT_MSGMERGE msgmerge HINTS ${amberedit_gettext_hints})

file(GLOB amberedit_po ${CMAKE_CURRENT_SOURCE_DIR}/po/*.po)
if(AMBEREDIT_MSGFMT)
  set(amberedit_mo "")
  foreach(po IN LISTS amberedit_po)
    get_filename_component(language ${po} NAME_WE)
    set(mo_dir ${CMAKE_BINARY_DIR}/locale/${language}/LC_MESSAGES)
    set(mo ${mo_dir}/amberedit.mo)
    add_custom_command(
      OUTPUT ${mo}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${mo_dir}
      # --check is what keeps a translation honest: it refuses a `.po` whose
      # header is malformed, whose plural forms do not match what the header
      # declares, or whose translation of a `{0}` message has lost the `{0}`.
      COMMAND ${AMBEREDIT_MSGFMT} --check --output-file=${mo} ${po}
      DEPENDS ${po}
      COMMENT "Compiling ${language}/LC_MESSAGES/amberedit.mo"
      VERBATIM)
    list(APPEND amberedit_mo ${mo})
    # ${CMAKE_INSTALL_LOCALEDIR} and not a directory of ours: this is the layout
    # libintl looks in, and putting a private copy of it under share/amberedit
    # would be the same three levels in a place no packaging tool knows about.
    # It is also what `%find_lang` in the spec walks.
    install(FILES ${mo}
            DESTINATION ${CMAKE_INSTALL_LOCALEDIR}/${language}/LC_MESSAGES)
  endforeach()
  # Part of `all`, so that a plain build leaves the catalogs beside the binary.
  add_custom_target(amberedit_catalogs ALL DEPENDS ${amberedit_mo})
elseif(amberedit_po)
  message(WARNING
    "msgfmt was not found, so the translations in po/ will not be built or "
    "installed and the interface will be in English. Install gettext to have "
    "them.")
endif()

# `cmake --build . --target pot` writes po/amberedit.pot out of the tree, and
# `--target update-po` merges it into every translation beside it. Neither runs
# as part of a build: what they touch is source, and a build does not edit its
# own inputs.
if(AMBEREDIT_XGETTEXT)
  file(GLOB_RECURSE amberedit_translatable
       RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
       ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp)
  list(SORT amberedit_translatable)
  # Relative names and a working directory in the source tree, so that the `#:`
  # lines in the .pot say `src/ui/find_dialog.cpp:38` and not where this
  # particular checkout happens to live.
  add_custom_target(pot
    COMMAND ${AMBEREDIT_XGETTEXT}
            --from-code=UTF-8 --language=C++ --sort-by-file
            # `_` and `C_` are the two the interface is written with, `N_` marks
            # a literal that is translated where it is drawn rather than where it
            # stands, and `plural` is `i18n::plural(one, many, n)`.
            --keyword=_ --keyword=N_ --keyword=C_:1c,2 --keyword=plural:1,2
            --add-comments=TRANSLATORS
            --package-name=AmberEdit --package-version=${PROJECT_VERSION}
            --copyright-holder=${PROJECT_NAME}
            --msgid-bugs-address=https://github.com/jegornet/amberedit/issues
            -o po/amberedit.pot
            ${amberedit_translatable}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Extracting po/amberedit.pot"
    VERBATIM)
endif()

if(AMBEREDIT_MSGMERGE AND amberedit_po)
  set(amberedit_merges "")
  foreach(po IN LISTS amberedit_po)
    get_filename_component(language ${po} NAME_WE)
    list(APPEND amberedit_merges
         COMMAND ${AMBEREDIT_MSGMERGE} --update --backup=none --quiet
                 ${po} ${CMAKE_CURRENT_SOURCE_DIR}/po/amberedit.pot)
  endforeach()
  add_custom_target(update-po ${amberedit_merges}
    COMMENT "Merging po/amberedit.pot into the translations"
    VERBATIM)
endif()

# --- tests -------------------------------------------------------------------
if(AMBEREDIT_BUILD_TESTS)
  # doctest from the distribution, like every other dependency: nothing is
  # fetched, so the tests build the same way with a network and without one,
  # and an rpmbuild or a sbuild gets them too.
  #
  # doctest and not Catch2 because it is the only one of the two that every
  # target packages under a single API. Catch2 has no such version: EPEL 8 —
  # the compiler floor — carries only the 2.13 series, while trixie, noble and
  # Homebrew carry only v3, and the two are not source-compatible. doctest is
  # 2.4.8 on EPEL and jammy and 2.5.x on Fedora and Homebrew, and the macros the
  # tests use are spelled the same in all of them.
  #
  # Not REQUIRED: its stock "could not find a package configuration file" says
  # nothing about which package to install or that the tests can simply be
  # turned off, and both are worth saying.
  find_package(doctest QUIET)

  if(NOT doctest_FOUND)
    message(FATAL_ERROR
      "doctest was not found. Install it — doctest-devel on RHEL and Fedora "
      "(from EPEL on RHEL), doctest-dev on Debian and Ubuntu, `brew install "
      "doctest` on macOS — or build without the tests: "
      "-DAMBEREDIT_BUILD_TESTS=OFF."
    )
  endif()
  message(STATUS "doctest ${doctest_VERSION} from the system")

  # doctest_discover_tests lives in doctest.cmake, installed beside the package
  # config; CMake does not put that directory on the module path itself.
  list(APPEND CMAKE_MODULE_PATH "${doctest_DIR}")

  enable_testing()
  add_subdirectory(tests)
endif()
