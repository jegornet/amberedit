#pragma once

#include <string>
#include <utility>

#include <tl/expected.hpp>

namespace amberedit {

/// How a fallible operation answers, everywhere in AmberEdit.
///
/// `Result<T>` is the value or the reason there is none, and there is no second
/// way: nothing throws, nothing keeps a `lastError()` to be asked afterwards,
/// and nothing returns a bool whose false the caller has to go looking for the
/// meaning of. The error is the message a person is to read, already complete —
/// naming the file, the line, the area — because the place that knows what went
/// wrong is the place that can say it, and every caller above it can only make
/// the sentence longer.
///
/// **tl::expected and not std::expected**, which is C++23 and the floor is RHEL
/// 8 and GCC 8. **The 1.0.0 API and no more of it**, because bookworm and jammy
/// carry 1.0.0 — so `has_value()`, `operator bool`, `operator*`, `error()`,
/// `value_or()` and `tl::make_unexpected`, and none of `transform`,
/// `transform_error` or the deduced `tl::unexpected`. `and_then` and `map` do go
/// back to 1.0.0 and are still not used: the code is written as plain statements
/// and a chain of lambdas would be the one place that is not.
///
/// **Read a Result through `*` after checking it, never through `value()`**,
/// which throws where the check would have said so.
///
/// **Every function returning a Result is `[[nodiscard]]` at its declaration.**
/// tl::expected marks itself so only from 1.2.0, which Fedora and Homebrew have
/// and no other target does, so on the version this must build against a
/// dropped Result is silent. Saying it at each declaration is the one spelling
/// that holds everywhere.
template <typename T>
using Result = tl::expected<T, std::string>;

/// The failure half of a Result, for `return failure("…")`. It converts to a
/// Result of any T, which is what lets a helper that only ever fails — and a
/// deep return out of a long function — say so without naming the type again.
[[nodiscard]] inline tl::unexpected<std::string> failure(std::string message) {
    return tl::make_unexpected(std::move(message));
}

}  // namespace amberedit
