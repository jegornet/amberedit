// The Catch2 runner, in a translation unit of its own.
//
// Catch2 v2 ships main() as a macro rather than as a library: the static
// Catch2::Catch2WithMain target exists only when Catch2 was built with
// CATCH_BUILD_STATIC_LIBRARY, and the packaged ones — EPEL 8's catch-devel
// among them — are not, so there is nothing to link against. Expanding the
// macro here costs one file and works the same whether Catch2 came from the
// system or was fetched.
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
