// The doctest runner, in a translation unit of its own.
//
// doctest is header-only and its main() comes from a macro that must be
// expanded in exactly one translation unit. Keeping it here rather than in a
// test file means none of them has to care which one is the runner, and the
// header is compiled without the implementation everywhere else.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
