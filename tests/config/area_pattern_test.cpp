#include <doctest/doctest.h>

#include <string>

#include "config/area_pattern.hpp"

using amberedit::config::AreaTagPattern;

namespace {

AreaTagPattern pattern(const std::string& text) {
    const auto parsed = AreaTagPattern::parse(text);
    REQUIRE(parsed);
    return *parsed;
}

bool hits(const std::string& text, const std::string& tag) {
    return pattern(text).matches(tag);
}

/// Whether the first pattern says more about a tag than the second.
bool beats(const std::string& a, const std::string& b) {
    return pattern(a).specificity() > pattern(b).specificity();
}

bool meets(const std::string& a, const std::string& b) {
    const bool forward = pattern(a).overlaps(pattern(b));
    // Overlapping is a question about a pair, so it must answer the same asked
    // either way round.
    CHECK(forward == pattern(b).overlaps(pattern(a)));
    return forward;
}

}  // namespace

TEST_CASE("A member pattern matches the tags it covers [area_pattern]") {
    CHECK(hits("esp.argentina", "esp.argentina"));
    CHECK_FALSE(hits("esp.argentina", "esp.argentin"));
    CHECK_FALSE(hits("esp.argentina", "esp.argentinaa"));

    CHECK(hits("esp.*", "esp.argentina"));
    // A dot is an ordinary character, so a star runs past one.
    CHECK(hits("esp.*", "esp.charla.libre"));
    // And the empty run is a run.
    CHECK(hits("esp.*", "esp."));
    CHECK_FALSE(hits("esp.*", "esp"));
    CHECK_FALSE(hits("esp.*", "pt.brasil"));

    CHECK(hits("*", "anything.at.all"));
    CHECK(hits("*.sysop", "r50.sysop"));
    CHECK(hits("*sysop*", "r50.sysop.talk"));

    CHECK(hits("r50.sysop?", "r50.sysops"));
    CHECK_FALSE(hits("r50.sysop?", "r50.sysop"));
    CHECK_FALSE(hits("r50.sysop?", "r50.sysopus"));
}

TEST_CASE("A member pattern folds case for ASCII and nothing else [area_pattern]") {
    CHECK(hits("ESP.*", "esp.argentina"));
    CHECK(hits("esp.*", "ESP.ARGENTINA"));

    // A tag in Cyrillic matches byte for byte: folding the high half of the
    // range is what a single-byte locale would do to us, and two differently
    // spelled tags would start comparing equal.
    CHECK(hits("ру.тест", "ру.тест"));
    CHECK_FALSE(hits("ру.тест", "РУ.ТЕСТ"));
}

TEST_CASE("The more particular member pattern outranks the wider one [area_pattern]") {
    CHECK(beats("esp.argentina", "esp.*"));
    CHECK(beats("esp.*.libre", "esp.*"));
    CHECK(beats("esp.*", "*.sysop"));
    CHECK(beats("*.sysop", "*"));
    // An exact tag is the more particular thing to have written, whatever else
    // says as much about the letters.
    CHECK(beats("esp.argentina", "esp.argentina?"));

    // The same pattern says exactly as much as itself, which is the tie the
    // config refuses.
    CHECK_FALSE(beats("esp.*", "esp.*"));
    CHECK_FALSE(beats("esp.*", "pt2.*"));
    CHECK_FALSE(beats("pt2.*", "esp.*"));
}

TEST_CASE("Two member patterns overlap when some tag matches both [area_pattern]") {
    CHECK(meets("esp.*", "esp.*"));
    CHECK(meets("esp.*", "*.argentina"));
    CHECK(meets("esp.*", "*"));
    CHECK(meets("esp.argentina", "esp.*"));
    CHECK(meets("*sp.*", "esp.*"));
    CHECK(meets("esp.?", "esp.a"));

    // Two patterns that could never meet, whatever the tosser declares.
    CHECK_FALSE(meets("esp.*", "pt.*"));
    CHECK_FALSE(meets("esp.argentina", "esp.chile"));
    CHECK_FALSE(meets("esp.?", "esp.ab"));
    CHECK_FALSE(meets("*.sysop", "*.talk"));
}

TEST_CASE("An empty member pattern is not a pattern [area_pattern]") {
    CHECK_FALSE(AreaTagPattern::parse(""));
    CHECK(AreaTagPattern::parse("*"));
}
