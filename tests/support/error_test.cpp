#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "support/error.hpp"
#include "test_strings.hpp"

using amberedit::ErrorPtr;
using amberedit::failure;
using amberedit::PlainError;

namespace {

/// A type that is neither trivially copyable nor default constructible, which
/// is the shape most of the project's answers hold — a parsed config, an opened
/// database — and the shape tl::expected's specialisations have historically
/// got wrong on old compilers.
struct Parsed {
    explicit Parsed(std::vector<std::string> lines) : lines(std::move(lines)) {}
    std::vector<std::string> lines;
};

tl::expected<Parsed, ErrorPtr> parse(bool ok) {
    if (!ok) return failure("nothing to parse");
    return Parsed{{"one", "two"}};
}

tl::expected<void, ErrorPtr> act(bool ok) {
    if (!ok) return failure("could not act");
    return {};
}

/// Three frames of the way out, which is the shallow end of what the config
/// parser does. Each one moves the error rather than copying it — and would not
/// compile if it tried to.
tl::expected<void, ErrorPtr> deep() {
    return failure("the reason, once");
}
tl::expected<int, ErrorPtr> deeper() {
    auto read = deep();
    if (!read) return tl::make_unexpected(std::move(read).error());
    return 1;
}
tl::expected<void, ErrorPtr> deepest() {
    auto read = deeper();
    if (!read) return tl::make_unexpected(std::move(read).error());
    return {};
}

}  // namespace

/// The whole of the tl::expected API AmberEdit is allowed to use, exercised in
/// one place so that a distro shipping something odd fails here by name rather
/// than in the middle of the tree. Nothing below is newer than 1.0.0, which is
/// what jammy carries: no transform, no transform_error, and tl::unexpected
/// written out with make_unexpected rather than deduced.
TEST_CASE("An answer carries a value or the reason there is none [support]") {
    const tl::expected<int, ErrorPtr> ok = 42;
    CHECK(ok.has_value());
    CHECK(*ok == 42);
    CHECK(ok.value_or(0) == 42);

    const tl::expected<int, ErrorPtr> bad = failure("no");
    CHECK_FALSE(bad.has_value());
    CHECK(bad.error()->message() == "no");
    CHECK(bad.value_or(7) == 7);
}

TEST_CASE("An answer holds a move-only-ish value [support]") {
    const auto parsed = parse(true);
    REQUIRE(parsed.has_value());
    CHECK(parsed->lines.size() == 2);
    CHECK(parsed->lines[1] == "two");

    const auto refused = parse(false);
    CHECK_FALSE(refused.has_value());
    CHECK(refused.error()->message() == "nothing to parse");
}

TEST_CASE("tl::expected<void, ErrorPtr> says whether it worked and why not [support]") {
    CHECK(act(true).has_value());
    CHECK_FALSE(act(false).has_value());
    CHECK(act(false).error()->message() == "could not act");
}

TEST_CASE("failure() converts to an expected of any T [support]") {
    // The one thing `failure` is for: a helper that only ever fails, and a deep
    // return out of a long function, saying so without naming the type again.
    const tl::expected<std::string, ErrorPtr> text = failure("the same reason");
    const tl::expected<std::vector<int>, ErrorPtr> numbers = failure("the same reason");
    CHECK(text.error()->message() == "the same reason");
    CHECK(numbers.error()->message() == "the same reason");
}

/// The error is a `unique_ptr`, and that is the point rather than an accident:
/// an answer is eight bytes of error however deep the stack it is handed up, and
/// the copy a `std::string` error invited at every frame will not compile.
TEST_CASE("The error is moved and never copied [support]") {
    CHECK(sizeof(tl::expected<void, ErrorPtr>) == sizeof(void*) * 2);
    CHECK_FALSE(std::is_copy_constructible<tl::expected<void, ErrorPtr>>::value);
    CHECK(std::is_move_constructible<tl::expected<void, ErrorPtr>>::value);

    const auto carried = deepest();
    REQUIRE_FALSE(carried.has_value());
    CHECK(carried.error()->message() == "the reason, once");
}

/// `failure("…")` builds a `PlainError`, and `failure<E>(…)` builds whichever
/// error class holds the parts. The second form is what lets a caller ask what
/// kind of failure it was instead of reading the sentence.
TEST_CASE("An error says what kind it is, not only what it reads as [support]") {
    const tl::expected<int, ErrorPtr> bad =
        failure<PlainError>(std::string("said plainly"));
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error()->message() == "said plainly");
    CHECK(dynamic_cast<const PlainError*>(bad.error().get()) != nullptr);
}

/// errorOf() is what the rest of the tests read an error through, so it is worth
/// pinning here rather than only where it is used.
TEST_CASE("errorOf reads the sentence, or empty where it worked [support]") {
    CHECK(amberedit::test::errorOf(act(true)).empty());
    CHECK(amberedit::test::errorOf(act(false)) == "could not act");
}
