#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "support/result.hpp"

using amberedit::failure;
using amberedit::Result;

namespace {

/// A type that is neither trivially copyable nor default constructible, which
/// is the shape most of the project's Results hold — a parsed config, an opened
/// database — and the shape tl::expected's specialisations have historically
/// got wrong on old compilers.
struct Parsed {
    explicit Parsed(std::vector<std::string> lines) : lines(std::move(lines)) {}
    std::vector<std::string> lines;
};

Result<Parsed> parse(bool ok) {
    if (!ok) return failure("nothing to parse");
    return Parsed{{"one", "two"}};
}

Result<void> act(bool ok) {
    if (!ok) return failure("could not act");
    return {};
}

}  // namespace

/// The whole of the tl::expected API AmberEdit is allowed to use, exercised in
/// one place so that a distro shipping something odd fails here by name rather
/// than in the middle of the tree. Nothing below is newer than 1.0.0, which is
/// what bookworm and jammy carry: no transform, no transform_error, and
/// tl::unexpected written out with make_unexpected rather than deduced.
TEST_CASE("Result carries a value or the reason there is none [support]") {
    const Result<int> ok = 42;
    CHECK(ok.has_value());
    CHECK(*ok == 42);
    CHECK(ok.value_or(0) == 42);

    const Result<int> bad = failure("no");
    CHECK_FALSE(bad.has_value());
    CHECK(bad.error() == "no");
    CHECK(bad.value_or(7) == 7);
}

TEST_CASE("Result holds a move-only-ish value [support]") {
    const auto parsed = parse(true);
    REQUIRE(parsed.has_value());
    CHECK(parsed->lines.size() == 2);
    CHECK(parsed->lines[1] == "two");

    const auto refused = parse(false);
    CHECK_FALSE(refused.has_value());
    CHECK(refused.error() == "nothing to parse");
}

TEST_CASE("Result<void> says whether it worked and why not [support]") {
    CHECK(act(true).has_value());
    CHECK_FALSE(act(false).has_value());
    CHECK(act(false).error() == "could not act");
}

TEST_CASE("failure() converts to a Result of any T [support]") {
    // The one thing `failure` is for: a helper that only ever fails, and a deep
    // return out of a long function, saying so without naming the type again.
    const auto message = failure("the same reason");
    const Result<std::string> text = message;
    const Result<std::vector<int>> numbers = message;
    CHECK(text.error() == "the same reason");
    CHECK(numbers.error() == "the same reason");
}
