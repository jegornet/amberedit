#pragma once

#include <exception>
#include <string>
#include <string_view>

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

/// The message of the std::runtime_error `run` throws, or "" if it throws none.
///
/// doctest's CHECK_THROWS_WITH compares the whole message and nothing less,
/// which none of these assertions want: the messages carry a file-and-line
/// prefix and run on past the part worth asserting. Catching the message and
/// putting `contains` to it says the same thing and says what was thrown when
/// it fails.
template <typename F>
std::string errorFrom(F&& run) {
    try {
        run();
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

}  // namespace amberedit::test
