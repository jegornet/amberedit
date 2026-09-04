# Cross-building the Windows binary from Linux or macOS with mingw-w64.
#
# The Windows dependencies are not packaged for the host, so they are built once
# into a prefix of their own and named here. AMBEREDIT_W64_PREFIX says where;
# `~/w64deps/prefix` is the default because that is what tools/build-w64-deps.sh
# writes, and nothing is ever looked for inside the source tree.
#
#   cmake -S . -B build-w64 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake
#
# The header-only dependencies — tl::expected and doctest — are architecture
# independent, so the host's own copies serve; hand their prefixes in with
# CMAKE_PREFIX_PATH as usual.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(AMBEREDIT_W64_TRIPLE x86_64-w64-mingw32 CACHE STRING "mingw-w64 target triple")

set(CMAKE_C_COMPILER ${AMBEREDIT_W64_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${AMBEREDIT_W64_TRIPLE}-g++)
set(CMAKE_RC_COMPILER ${AMBEREDIT_W64_TRIPLE}-windres)

if(NOT DEFINED AMBEREDIT_W64_PREFIX)
  set(AMBEREDIT_W64_PREFIX "$ENV{HOME}/w64deps/prefix")
endif()
list(APPEND CMAKE_FIND_ROOT_PATH "${AMBEREDIT_W64_PREFIX}")
list(APPEND CMAKE_PREFIX_PATH "${AMBEREDIT_W64_PREFIX}")

# Programs come from the host — the compiler driver, msgfmt, xgettext. Headers
# and libraries must not: finding the host's libiconv here would configure
# cleanly and fail at link, which is the failure mode this setting exists for.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# Static throughout, so that what comes out is one .exe a user can copy rather
# than an .exe and a handful of DLLs from the build host.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
