cmake_minimum_required(VERSION 3.16)
project(AmberEdit VERSION 0.1.2 LANGUAGES CXX C)

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

include(FetchContent)

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
#   * whether what was found actually has the wide and extended-colour API.
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
# characters and extended colour pairs? One compile answers all three.
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

# --- application core --------------------------------------------------------
add_library(amberedit_core STATIC
  src/domain/ftn_address.cpp
  src/domain/address_pattern.cpp
  src/domain/area.cpp
  src/domain/message.cpp
  src/config/text_util.cpp
  src/config/temp_dir.cpp
  src/config/cfg_file.cpp
  src/config/list_format.cpp
  src/config/area_pattern.cpp
  src/config/app_config.cpp
  src/config/fidoconfig_parser.cpp
  src/config/manual_area_source.cpp
  src/config/areas_bbs_parser.cpp
  src/config/squish_cfg_parser.cpp
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
  src/app/navigator.cpp
)
# The version reaches the code through a generated header, so that
# project(VERSION) above stays the one place it is written down. The system is
# taken at configure time rather than from the preprocessor: it is the system
# AmberEdit is built for, which is what a message signed "AmberEdit/linux" claims.
string(TOLOWER "${CMAKE_SYSTEM_NAME}" AMBEREDIT_SYSTEM)
configure_file(src/version.hpp.in ${CMAKE_BINARY_DIR}/generated/version.hpp @ONLY)

target_include_directories(amberedit_core PUBLIC src ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(amberedit_core
  PRIVATE amberedit_iconv
  # PUBLIC because everything linking the core — the executable and the tests —
  # uses std::filesystem too, and on GCC 8 that is an extra -l rather than free.
  PUBLIC amberedit_filesystem
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
  src/ui/bbs_codes.cpp
  src/ui/area_list_format.cpp
  src/ui/msg_list_format.cpp
  src/ui/confirm_dialog.cpp
  src/ui/dialog_frame.cpp
  src/ui/dir_listing.cpp
  src/ui/error_dialog.cpp
  src/ui/export_dialog.cpp
  src/ui/export_mode_dialog.cpp
  src/ui/find_dialog.cpp
  src/ui/attributes_dialog.cpp
  src/ui/forward_dialog.cpp
  src/ui/hint_bar.cpp
  src/ui/import_dialog.cpp
  src/ui/info_dialog.cpp
  src/ui/input_field.cpp
  src/ui/keys.cpp
  src/ui/menu_dialog.cpp
  src/ui/nodelist_dialog.cpp
  src/ui/reply_dialog.cpp
  src/ui/rescan_dialog.cpp
  src/ui/text_layout.cpp
  src/ui/text_editor.cpp
  src/ui/edit_layout.cpp
  src/ui/theme.cpp
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
include(GNUInstallDirs)
install(TARGETS amberedit RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES default.tpl DESTINATION ${CMAKE_INSTALL_DATADIR}/amberedit)
# The exclusion is not paranoia: `._default.cfg` beside `default.cfg` matches
# `*.cfg` and would be installed as a theme. macOS writes those sidecars whenever
# a file with extended attributes is carried through an archive, so a package
# built from a tarball made on a Mac ships them.
install(DIRECTORY themes/ DESTINATION ${CMAKE_INSTALL_DATADIR}/amberedit/themes
        FILES_MATCHING PATTERN "*.cfg" PATTERN "._*" EXCLUDE)

# --- tests -------------------------------------------------------------------
if(AMBEREDIT_BUILD_TESTS)
  # An installed Catch2 is preferred over a fetched one. Distributions package
  # this very series — EPEL 8's catch-devel is 2.13.8, the tag pinned below —
  # and an rpmbuild has no network to fetch with, so the package is the only
  # way the tests get built there at all.
  #
  # The 2.13 series and no other: v3 moved the headers and dropped
  # CATCH_CONFIG_MAIN, so it is not a drop-in and a bare version floor would
  # let it through. Only Catch2::Catch2 is used — see tests/main.cpp for why
  # not Catch2::Catch2WithMain.
  find_package(Catch2 QUIET)
  if(Catch2_FOUND AND (Catch2_VERSION VERSION_LESS 2.13 OR
                       Catch2_VERSION VERSION_GREATER_EQUAL 3))
    message(STATUS
      "Catch2 ${Catch2_VERSION} is installed, but the tests are written against "
      "the 2.13 series; fetching that instead")
    set(Catch2_FOUND FALSE)
  endif()

  if(Catch2_FOUND)
    message(STATUS "Catch2 ${Catch2_VERSION} from the system")
    # catch_discover_tests lives in Catch.cmake, installed beside the package
    # config; CMake does not put that directory on the module path itself.
    list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")
  else()
    # EXCLUDE_FROM_ALL keeps Catch2's own install and packaging targets out of
    # the default build, but FetchContent_Declare only learned the keyword in
    # CMake 3.28 and older CMake passes it down to ExternalProject_Add, which
    # rejects it. RHEL 8 is well below 3.28, so it is added only where it is
    # understood.
    set(amberedit_catch2_args "")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
      set(amberedit_catch2_args EXCLUDE_FROM_ALL)
    endif()
    FetchContent_Declare(
      Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2
      GIT_TAG v2.13.8
      GIT_SHALLOW TRUE
      ${amberedit_catch2_args}
    )
    FetchContent_MakeAvailable(Catch2)
    # The same module, which a clone keeps in contrib/ instead.
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/contrib")
  endif()

  enable_testing()
  add_subdirectory(tests)
endif()
