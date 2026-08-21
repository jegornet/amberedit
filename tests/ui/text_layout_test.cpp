#include <catch2/catch.hpp>

#include <string>
#include <vector>

#include "ui/term/utf8.hpp"
#include "ui/text_layout.hpp"

using namespace amberedit::ui;

TEST_CASE("displayWidth counts columns, not bytes or code points", "[layout]") {
    CHECK(displayWidth("") == 0);
    CHECK(displayWidth("abc") == 3);
    // Twelve bytes, six code points, six columns.
    CHECK(displayWidth("Привет") == 6);
    // Three code points but six columns: an ideograph takes two cells, and the
    // renderer draws it in two whatever we count. Counting code points here is
    // what used to push the message list's columns out of line.
    CHECK(displayWidth("日本語") == 6);
    // A combining accent takes none: "e" then U+0301 is one column.
    CHECK(displayWidth("e\u0301") == 1);
}

TEST_CASE("displayWidth agrees with what the renderer will draw", "[layout]") {
    // The measuring here and the dividing the renderer does have to agree, or a
    // cell budgeted for is not a cell drawn. toGlyphs() hands back exactly one
    // entry per column it will occupy — the second cell of a wide glyph among
    // them — so its count is the width, and this is the invariant that ties the
    // layout to what actually reaches the screen.
    for (const char* sample : {"abc", "Привет", "日本語", "e\u0301", "AmberEdit — a reader"}) {
        INFO(sample);
        CHECK(static_cast<int>(amberedit::ui::term::toGlyphs(sample).size()) ==
              displayWidth(sample));
    }
}

TEST_CASE("truncateToWidth cuts on character boundaries", "[layout]") {
    CHECK(truncateToWidth("Привет", 10) == "Привет");
    CHECK(truncateToWidth("Привет", 6) == "Привет");
    CHECK(truncateToWidth("Привет", 4) == "При…");
    CHECK(truncateToWidth("Привет", 1) == "…");
    CHECK(truncateToWidth("Привет", 0).empty());
}

TEST_CASE("truncateToWidth never leaves half a double-width glyph", "[layout]") {
    // 日本語 is six columns. Cut to five, the budget is four after the ellipsis,
    // which is two whole ideographs — the third must not be half-drawn.
    CHECK(truncateToWidth("日本語", 6) == "日本語");
    CHECK(truncateToWidth("日本語", 5) == "日本…");
    // Four columns leaves three for text: one ideograph, and the odd column
    // stays blank rather than holding half of the next.
    CHECK(truncateToWidth("日本語", 4) == "日…");
    CHECK(displayWidth(truncateToWidth("日本語", 4)) <= 4);
    CHECK(displayWidth(truncateToWidth("日本語", 5)) <= 5);
}

TEST_CASE("padRight pads to columns, not to code points", "[layout]") {
    // Two ideographs are four columns, so eight columns need four spaces —
    // counting code points would have added six and pushed the row out.
    CHECK(displayWidth(padRight("日本", 8)) == 8);
    CHECK(padRight("日本", 8) == "日本    ");
}

TEST_CASE("padRight/padLeft align Cyrillic", "[layout]") {
    // The whole point: the 6 characters of "Привет" occupy 12 bytes, and the
    // padding has to reach a width in characters or the columns drift apart.
    CHECK(padRight("Привет", 8) == "Привет  ");
    CHECK(padLeft("Привет", 8) == "  Привет");
    CHECK(padRight("Привет", 3) == "Привет");  // never shortens
    CHECK(displayWidth(padRight("ru.linux", 12)) == 12);
}

TEST_CASE("digitWidth counts digits", "[layout]") {
    CHECK(digitWidth(0) == 1);
    CHECK(digitWidth(9) == 1);
    CHECK(digitWidth(10) == 2);
    CHECK(digitWidth(12345) == 5);
}

TEST_CASE("horizontalRule is as wide as asked, in characters", "[layout]") {
    CHECK(displayWidth(horizontalRule(10)) == 10);
    CHECK(displayWidth(horizontalRule(1)) == 1);
    CHECK(horizontalRule(3) == "───");
    // A degenerate width still yields something drawable rather than nothing.
    CHECK(displayWidth(horizontalRule(0)) == 1);
    CHECK(displayWidth(horizontalRule(-5)) == 1);
}

TEST_CASE("quoteDepth counts the markers a quote opens with", "[quote]") {
    CHECK(quoteDepth("> text") == 1);
    CHECK(quoteDepth(">> text") == 2);
    CHECK(quoteDepth(">>> text") == 3);
    CHECK(quoteDepth("> ") == 1);  // nothing quoted, but still a quote line
}

TEST_CASE("quoteDepth accepts optional initials", "[quote]") {
    CHECK(quoteDepth("AB> text") == 1);
    CHECK(quoteDepth(" AB> text") == 1);
    CHECK(quoteDepth(" VP>>> text") == 3);
    CHECK(quoteDepth("ABCDEF> text") == 1);   // six letters is the limit
    CHECK(quoteDepth("ABCDEFG> text") == 0);  // seven is too many
}

TEST_CASE("quoteDepth accepts Cyrillic initials", "[quote]") {
    // Russian echoes write initials in Cyrillic as a matter of course, so the
    // letters have to be counted in code points rather than bytes.
    CHECK(quoteDepth("ЕГ> текст") == 1);
    CHECK(quoteDepth(" ЕГ>> текст") == 2);
    CHECK(quoteDepth("АБВГДЕ> текст") == 1);
    CHECK(quoteDepth("АБВГДЕЖ> текст") == 0);
}

TEST_CASE("quoteDepth allows up to two leading spaces", "[quote]") {
    CHECK(quoteDepth(" > text") == 1);
    CHECK(quoteDepth("  > text") == 1);
    CHECK(quoteDepth("   > text") == 0);  // three is indentation, not a quote
}

TEST_CASE("quoteDepth requires the space after the markers", "[quote]") {
    CHECK(quoteDepth(">text") == 0);
    CHECK(quoteDepth(">>>text") == 0);
    CHECK(quoteDepth("AB>text") == 0);
    CHECK(quoteDepth(">") == 0);
    CHECK(quoteDepth(">>>") == 0);
}

TEST_CASE("quoteDepth reads the '->' QWK gateways quote with", "[quote]") {
    CHECK(quoteDepth("-> text") == 1);
    CHECK(quoteDepth(" -> text") == 1);
    CHECK(quoteDepth("->> text") == 2);
    CHECK(quoteDepth("AB-> text") == 1);
    CHECK(quoteDepth("->text") == 0);  // the space is still required
    CHECK(quoteDepth("--> text") == 0);
}

TEST_CASE("quoteDepth rejects lines with no markers", "[quote]") {
    CHECK(quoteDepth("") == 0);
    CHECK(quoteDepth("ordinary text") == 0);
    CHECK(quoteDepth("- not a quote") == 0);
    CHECK(quoteDepth("1> not a quote") == 0);  // digits are not initials
    CHECK(quoteDepth(" * Origin: somewhere (2:5020/1)") == 0);
}

TEST_CASE("wrapText keeps short lines as they are", "[layout]") {
    // Indentation and quoting must not be normalised away.
    const auto lines = wrapText("  AB> quoted with indent\nan ordinary line", 40);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "  AB> quoted with indent");
    CHECK(lines[1] == "an ordinary line");
}

TEST_CASE("wrapText keeps blank lines", "[layout]") {
    const auto lines = wrapText("first\n\nsecond", 40);
    REQUIRE(lines.size() == 3);
    CHECK(lines[1].empty());
}

TEST_CASE("wrapText breaks long lines on word boundaries", "[layout]") {
    const auto lines = wrapText("aaa bbb ccc ddd", 7);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "aaa bbb");
    CHECK(lines[1] == "ccc ddd");
}

TEST_CASE("wrapText keeps the indentation of a line it breaks", "[layout]") {
    // A quote that does not fit is wrapped, not shifted left: the space it
    // opens with is part of the line, not a separator to be skipped over.
    const auto lines = wrapText(" YG>> Mon Jul 27 01:18:52 CEST 2026", 20);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == " YG>> Mon Jul 27");
    CHECK(lines[1] == "01:18:52 CEST 2026");
}

TEST_CASE("wrapText keeps the spacing inside a line it breaks", "[layout]") {
    const auto lines = wrapText("--- tosser 1.0   + Origin: here (2:5020/1)", 20);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "--- tosser 1.0   +");
    CHECK(lines[1] == "Origin: here");
    CHECK(lines[2] == "(2:5020/1)");
}

TEST_CASE("wrapText splits a word longer than the width", "[layout]") {
    const auto lines = wrapText("aaaaaaaaaa", 4);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "aaaa");
    CHECK(lines[1] == "aaaa");
    CHECK(lines[2] == "aa");
}

TEST_CASE("wrapText does not exceed the width on Cyrillic", "[layout]") {
    const auto lines =
        wrapText("Съешь ещё этих мягких французских булок да выпей чаю", 12);
    REQUIRE_FALSE(lines.empty());
    for (const auto& line : lines) {
        INFO(line);
        CHECK(displayWidth(line) <= 12);
    }
}

TEST_CASE("wrapText with a zero width returns nothing", "[layout]") {
    CHECK(wrapText("text", 0).empty());
    CHECK(wrapText("text", -5).empty());
}

TEST_CASE("softWrapOffsets divides a line rather than rewriting it", "[layout]") {
    using Offsets = std::vector<size_t>;
    // A line that fits is one row, beginning where it begins.
    CHECK(softWrapOffsets("aaa bbb", 7) == Offsets{0});
    // The blank the break falls on closes the row it is on, so that no byte is
    // left without a row — what `wrapText` drops, this keeps.
    CHECK(softWrapOffsets("aaa bbb ccc ddd", 7) == Offsets{0, 8});
    // A word with nowhere to break is cut where the width falls.
    CHECK(softWrapOffsets("aaaaaaaaaa", 4) == Offsets{0, 4, 8});
    CHECK(softWrapOffsets("aaaa bbbbbbbbbb", 6) == Offsets{0, 5, 11});
    // Nothing to lay out on, so nothing is laid out: one row, as it stands.
    CHECK(softWrapOffsets("text", 0) == Offsets{0});
}

TEST_CASE("softWrapOffsets does not exceed the width on Cyrillic", "[layout]") {
    const std::string line = "Съешь ещё этих мягких французских булок да выпей чаю";
    const auto starts = softWrapOffsets(line, 12);
    REQUIRE(starts.size() > 1);
    for (size_t i = 0; i < starts.size(); ++i) {
        const size_t end = i + 1 < starts.size() ? starts[i + 1] : line.size();
        // The blanks a row ends with may run past the edge — they draw nothing
        // there — so what is measured is the row with them taken off.
        std::string row = line.substr(starts[i], end - starts[i]);
        while (!row.empty() && row.back() == ' ') row.pop_back();
        INFO(row);
        CHECK(displayWidth(row) <= 12);
    }
}

namespace {

/// The links of a line, as the substrings they cover — easier to read in a
/// failure than a pair of offsets.
std::vector<std::string> linksIn(const std::string& line) {
    std::vector<std::string> out;
    for (const auto& [begin, end] : amberedit::ui::findLinks(line)) {
        out.push_back(line.substr(begin, end - begin));
    }
    return out;
}

}  // namespace

TEST_CASE("findLinks picks out the schemes it knows", "[layout]") {
    CHECK(linksIn("see https://google.com for details") ==
          std::vector<std::string>{"https://google.com"});
    CHECK(linksIn("http://ftn.example/x?a=1&b=2") ==
          std::vector<std::string>{"http://ftn.example/x?a=1&b=2"});
    CHECK(linksIn("ftp://ftp.funet.fi/pub/") ==
          std::vector<std::string>{"ftp://ftp.funet.fi/pub/"});
}

TEST_CASE("findLinks finds every link on a line", "[layout]") {
    CHECK(linksIn("http://a.example and http://b.example") ==
          std::vector<std::string>{"http://a.example", "http://b.example"});
}

TEST_CASE("findLinks leaves the sentence's punctuation out of the link", "[layout]") {
    CHECK(linksIn("go to https://google.com.") ==
          std::vector<std::string>{"https://google.com"});
    CHECK(linksIn("(see http://x.example)") ==
          std::vector<std::string>{"http://x.example"});
    CHECK(linksIn("quoted \"http://x.example\", then") ==
          std::vector<std::string>{"http://x.example"});
    // A bracket the address opened itself belongs to it.
    CHECK(linksIn("http://wiki.example/Foo_(bar)") ==
          std::vector<std::string>{"http://wiki.example/Foo_(bar)"});
}

TEST_CASE("findLinks does not guess at addresses without a scheme", "[layout]") {
    // Coloring these would mean coloring ordinary words: a message that
    // mentions google.com is not offering a link.
    CHECK(linksIn("google.com is a search engine").empty());
    CHECK(linksIn("www.example.org").empty());
    CHECK(linksIn("mail me at user@example.org").empty());
}

TEST_CASE("findLinks needs the scheme to start a word", "[layout]") {
    CHECK(linksIn("xhttp://example.org").empty());
    // The scheme on its own is not an address.
    CHECK(linksIn("http:// and nothing after").empty());
}

namespace {

/// The emphasised phrases of a line, as "marker:text" — easier to read in a
/// failure than a marker and a pair of offsets.
std::vector<std::string> stylesIn(const std::string& line) {
    std::vector<std::string> out;
    for (const auto& span : findStyleSpans(line)) {
        out.push_back(std::string(1, span.marker) + ":" +
                      line.substr(span.begin, span.end - span.begin));
    }
    return out;
}

}  // namespace

TEST_CASE("findStyleSpans picks out the three markers", "[layout]") {
    CHECK(stylesIn("say _this_ now") == std::vector<std::string>{"_:_this_"});
    CHECK(stylesIn("say *this* now") == std::vector<std::string>{"*:*this*"});
    CHECK(stylesIn("say /this/ now") == std::vector<std::string>{"/:/this/"});
    CHECK(stylesIn("say #this# now") == std::vector<std::string>{"#:#this#"});
    // At either end of the line, and around several words at once.
    CHECK(stylesIn("*a whole phrase*") == std::vector<std::string>{"*:*a whole phrase*"});
}

TEST_CASE("findStyleSpans finds every phrase on a line", "[layout]") {
    CHECK(stylesIn("*one* and _two_") ==
          std::vector<std::string>{"*:*one*", "_:_two_"});
    // The first marker to open wins; what its phrase holds is taken as written.
    CHECK(stylesIn("*_both_*") == std::vector<std::string>{"*:*_both_*"});
}

TEST_CASE("findStyleSpans needs the markers to stand outside words", "[layout]") {
    CHECK(stylesIn("snake_case_name").empty());
    CHECK(stylesIn("2*3*4").empty());
    CHECK(stylesIn("/usr/local/bin/gcc").empty());
    CHECK(stylesIn("#include <stdio.h>").empty());
    CHECK(stylesIn("see #1 and #2").empty());
    CHECK(stylesIn("a * b * c").empty());
    CHECK(stylesIn("half *open and nothing after").empty());
    CHECK(stylesIn("*").empty());
    CHECK(stylesIn("**").empty());
}

TEST_CASE("findStyleSpans allows brackets and punctuation around a phrase",
          "[layout]") {
    CHECK(stylesIn("(*bold*)") == std::vector<std::string>{"*:*bold*"});
    CHECK(stylesIn("well, _really_?") == std::vector<std::string>{"_:_really_"});
}

TEST_CASE("findStyleSpans closes a phrase at the first marker that ends a word",
          "[layout]") {
    CHECK(stylesIn("*one* two*") == std::vector<std::string>{"*:*one*"});
}
