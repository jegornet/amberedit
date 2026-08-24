#pragma once

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <utility>

#include "support/error.hpp"

namespace amberedit::test {

/// `text` appears somewhere in `haystack`.
///
/// doctest has no matchers, so a substring assertion is an ordinary predicate.
/// Pair it with CHECK_MESSAGE and pass the haystack as the message: a bare
/// CHECK would report only "false", where this prints the string that was
/// searched.
inline bool contains(std::string_view haystack, std::string_view text) {
    return haystack.find(text) != std::string_view::npos;
}

/// The reason an answer holds no value, or "" where it holds one.
///
/// doctest has no matchers and none of these assertions wants a whole message
/// compared: they carry a file-and-line prefix and run on past the part worth
/// asserting. So this is paired with `contains` and CHECK_MESSAGE, which prints
/// the message when the assertion fails.
template <typename T>
std::string errorOf(const tl::expected<T, ErrorPtr>& result) {
    return result ? std::string{} : result.error()->message();
}

/// The value of an answer a test says must hold one.
///
/// REQUIRE and not CHECK: a test whose premise has broken stops there rather
/// than going on to read a value that is not there. By value because the expected
/// it comes out of is usually a temporary — and moved out of where it is one,
/// which is what lets an expected of a unique_ptr through at all.
template <typename T>
T valueOf(tl::expected<T, ErrorPtr>&& result) {
    REQUIRE_MESSAGE(result.has_value(), errorOf(result));
    return std::move(*result);
}

template <typename T>
T valueOf(const tl::expected<T, ErrorPtr>& result) {
    REQUIRE_MESSAGE(result.has_value(), errorOf(result));
    return *result;
}

}  // namespace amberedit::test
