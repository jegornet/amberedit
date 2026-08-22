#include <doctest/doctest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "test_paths.hpp"
#include "ui/ansi_canvas.hpp"
#include "ui/text_layout.hpp"

namespace ansi = amberedit::ui::ansi;
namespace bbs = amberedit::ui::bbs;

namespace {

/// The canvas as plain rows, which is what most of these are about: where the
/// glyphs ended up after the message moved the cursor around.
std::vector<std::string> textOf(const std::vector<bbs::CodedLine>& lines) {
    std::vector<std::string> out;
    out.reserve(lines.size());
    for (const auto& line : lines) out.push_back(line.text);
    return out;
}

/// The color runs of one row as "offset:fg/bg", the same shorthand the pipe
/// code tests read in.
std::vector<std::string> runsOf(const std::vector<bbs::ColorRun>& runs) {
    std::vector<std::string> out;
    out.reserve(runs.size());
    for (const auto& run : runs) {
        out.push_back(std::to_string(run.begin) + ":" + std::to_string(run.color.fg) +
                      "/" + std::to_string(run.color.bg));
    }
    return out;
}

std::vector<bbs::CodedLine> draw(const std::string& stream) {
    return ansi::render(stream, ansi::kColumns);
}

}  // namespace

TEST_CASE("containsCodes finds the sequences and nothing else [ansi]") {
    CHECK(ansi::containsCodes("\x1b[31mred"));
    CHECK(ansi::containsCodes("\x1b[2J"));
    CHECK(ansi::containsCodes("\x1b[12;30Hhere"));
    CHECK(ansi::containsCodes("\x1b]0;title\x07"));
    CHECK(ansi::containsCodes("\x1b]0;title\x1b\\"));
    CHECK(ansi::containsCodes("save \x1b" "7 here"));
    CHECK(ansi::containsCodes("\x1bM"));

    // Plain text, however much punctuation it has in it.
    CHECK_FALSE(ansi::containsCodes("[31m is not a code, and nor is |07"));
    // An ESC that opens nothing: a message ending mid-sequence, and one that
    // simply has the byte in it.
    CHECK_FALSE(ansi::containsCodes("\x1b"));
    CHECK_FALSE(ansi::containsCodes("\x1b[31"));
    CHECK_FALSE(ansi::containsCodes("\x1b" "x"));
    CHECK_FALSE(ansi::containsCodes("\x1b]0;unterminated"));
}

TEST_CASE("a line break is the carriage return and the line feed [ansi]") {
    // Nothing else would do: the art is written as chunks that step back up
    // with an ESC[A, and each of them counts on starting at column one.
    CHECK(textOf(draw("one\ntwo")) == std::vector<std::string>{"one", "two"});
    CHECK(textOf(draw("abcdef\n\x1b[A\x1b[3Cxyz")) ==
          std::vector<std::string>{"abcxyz"});
}

TEST_CASE("the cursor moves the message about the canvas [ansi]") {
    CHECK(textOf(draw("\x1b[3;5Hx")) == std::vector<std::string>{"", "", "    x"});
    CHECK(textOf(draw("ab\x1b[2Dz")) == std::vector<std::string>{"zb"});
    CHECK(textOf(draw("a\x1b[2Cb")) == std::vector<std::string>{"a  b"});
    CHECK(textOf(draw("a\x1b[Bb")) == std::vector<std::string>{"a", " b"});
    CHECK(textOf(draw("ab\x1b[Eq")) == std::vector<std::string>{"ab", "q"});
    // CPL goes up and to column one, and what it lands on it draws over.
    CHECK(textOf(draw("ab\n\x1b[Fq")) == std::vector<std::string>{"qb"});
    CHECK(textOf(draw("abc\x1b[2Gz")) == std::vector<std::string>{"azc"});
    // Up at the top edge and left at the left one stay where they are rather
    // than wrapping round to somewhere else in the picture.
    CHECK(textOf(draw("x\x1b[9A\x1b[9Dz")) == std::vector<std::string>{"z"});
}

TEST_CASE("output past the last column wraps at once [ansi]") {
    // Which is the whole reason the canvas has a width at all. The art draws a
    // border down column 80 and expects what follows on the next row, and a
    // wrap that a following cursor move could cancel would put it back on top
    // of the border.
    const std::string stream = "\x1b[80Gx\x1b[1Cy";
    const auto lines = draw(stream);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].text.size() == 80);
    CHECK(lines[0].text.substr(79) == "x");
    CHECK(lines[1].text == " y");
}

TEST_CASE("the cursor stops at the right-hand edge [ansi]") {
    CHECK(draw("\x1b[999Cx")[0].text.size() == 80);
    CHECK(draw("\x1b[1;999Hx")[0].text.size() == 80);
}

TEST_CASE("save and restore put the cursor back [ansi]") {
    CHECK(textOf(draw("ab\x1b" "7\x1b[5;5Hx\x1b" "8z")) ==
          std::vector<std::string>{"abz", "", "", "", "    x"});
    // The CSI spelling of the same pair, and the one BBS art is actually
    // written with. A message may use either.
    CHECK(textOf(draw("ab\x1b[s\x1b[5;5Hx\x1b[uz")) ==
          std::vector<std::string>{"abz", "", "", "", "    x"});
    // Which is the other way a file carries a row on across its own newline:
    // save, let the break fall, restore.
    CHECK(textOf(draw("ab\x1b[s\n\x1b[ucd")) == std::vector<std::string>{"abcd"});
}

TEST_CASE("the colors are the terminal's own order [ansi]") {
    // Unlike the pipe codes, which count in the DOS order: SGR numbers red 31
    // and blue 34, and the terminal numbers them 1 and 4.
    CHECK(runsOf(draw("\x1b[31mr")[0].runs) == std::vector<std::string>{"0:1/-1"});
    CHECK(runsOf(draw("\x1b[34mb")[0].runs) == std::vector<std::string>{"0:4/-1"});
    CHECK(runsOf(draw("\x1b[36;41mx")[0].runs) == std::vector<std::string>{"0:6/1"});
    // 39 and 49 hand each half back to the theme.
    CHECK(runsOf(draw("\x1b[31;44mx\x1b[39;49my")[0].runs) ==
          std::vector<std::string>{"0:1/4", "1:-1/-1"});
}

TEST_CASE("bold is the bright half of the palette [ansi]") {
    // Not a weight: on the adapter these codes were written for, 1 set the
    // intensity bit of the color, and art is drawn with sixteen colors and not
    // with eight in two weights.
    CHECK(draw("\x1b[1;32mx")[0].runs.front().color.fg == 10);
    CHECK(draw("\x1b[32;1mx")[0].runs.front().color.fg == 10);
    // With no color named, the default foreground brightens: light grey to
    // white, which is what a picture asking for ESC[1m alone means by it.
    CHECK(draw("\x1b[1mx")[0].runs.front().color.fg == 15);
    // And 21 takes it off again.
    CHECK(runsOf(draw("\x1b[1;32ma\x1b[21mb")[0].runs) ==
          std::vector<std::string>{"0:10/-1", "1:2/-1"});
}

TEST_CASE("inversion exchanges the two halves [ansi]") {
    CHECK(runsOf(draw("\x1b[31;44ma\x1b[7mb\x1b[27mc")[0].runs) ==
          std::vector<std::string>{"0:1/4", "1:4/1", "2:1/4"});
    // Neither half named: they have to be made concrete to be exchanged, or the
    // inversion would show nothing at all.
    CHECK(runsOf(draw("\x1b[7mx")[0].runs) == std::vector<std::string>{"0:0/7"});
}

TEST_CASE("concealed text is written as the blank it is drawn as [ansi]") {
    // 28 turns the attribute off for what comes after it and says nothing about
    // what is already on the canvas, so it cannot be decided at drawing time.
    const auto lines = draw("\x1b[8mhide\x1b[28mshow");
    CHECK(lines[0].text == "    show");
}

TEST_CASE("blinking is passed over [ansi]") {
    // Nothing in a reader should flash. The parameter is neither acted on nor
    // drawn: it leaves no mark of any kind.
    CHECK(textOf(draw("\x1b[5;31mx")) == std::vector<std::string>{"x"});
    CHECK(draw("\x1b[5;31mx")[0].runs.front().color.fg == 1);
}

TEST_CASE("the erasers clear what the picture has already drawn [ansi]") {
    CHECK(textOf(draw("abcdef\x1b[1;4H\x1b[0K")) == std::vector<std::string>{"abc"});
    // To *and including* the cursor, which is how ECMA-48 has both the line
    // eraser and the screen one below.
    CHECK(textOf(draw("abcdef\x1b[1;4H\x1b[1K")) == std::vector<std::string>{"    ef"});
    CHECK(textOf(draw("abcdef\x1b[2K")).empty());
    CHECK(textOf(draw("ab\ncd\ref\x1b[1;2H\x1b[0J")) == std::vector<std::string>{"a"});
    CHECK(textOf(draw("ab\ncd\x1b[2;2H\x1b[1J")).empty());
    CHECK(textOf(draw("ab\ncd\x1b[2J")).empty());
}

TEST_CASE("an erase leaves the background in force behind it [ansi]") {
    // Which is what a picture painting a band of color across the screen counts
    // on: the cleared cells carry the color, so they are not blank and are not
    // dropped off the end of the row.
    const auto lines = draw("\x1b[44m\x1b[1;5Hx\x1b[1;1H\x1b[0K");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].text == "     ");
    CHECK(runsOf(lines[0].runs) == std::vector<std::string>{"0:-1/4"});
}

TEST_CASE("a row keeps only what was drawn on it [ansi]") {
    // No padding to the canvas width: a row of blanks would paint the theme's
    // background out to the edge of the window on every line of the picture.
    const auto lines = draw("\x1b[1;40Hx");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].text.size() == 40);
    // And the rows the cursor merely passed over on its way out go too.
    CHECK(textOf(draw("x\x1b[10B")) == std::vector<std::string>{"x"});
}

TEST_CASE("a narrow window cuts the rows rather than wrapping them [ansi]") {
    const auto lines = ansi::render("\x1b[31mabcdef", 3);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].text == "abc");
    CHECK(runsOf(lines[0].runs) == std::vector<std::string>{"0:1/-1"});
}

TEST_CASE("sequences that say nothing here leave no bytes behind [ansi]") {
    // A code this reader has no answer for is still a code, and drawing its
    // bytes would be worse than ignoring it.
    CHECK(textOf(draw("a\x1b[?25lb")) == std::vector<std::string>{"ab"});
    CHECK(textOf(draw("a\x1b]0;window title\x07" "b")) == std::vector<std::string>{"ab"});
    CHECK(textOf(draw("a\x1bMb")) == std::vector<std::string>{"ab"});
    // An ESC that opens nothing loses only itself: what follows it was never
    // part of a sequence and is the message's own text.
    CHECK(textOf(draw("a\x1b" "xb")) == std::vector<std::string>{"axb"});
}

TEST_CASE("a message cannot make the canvas grow without bound [ansi]") {
    const auto lines = draw("x\x1b[99999999999Bz");
    CHECK(static_cast<int>(lines.size()) == ansi::kMaxRows);
}

TEST_CASE("the sample artwork carried on its saved cursor replays too [ansi]") {
    // The second file, which says with ESC[s and ESC[u what the first says with
    // ESC[A: without them every chunk starts a row of its own and the picture
    // falls into strips.
    std::ifstream file(amberedit::test::projectPath("testdata/ansi/ansi_msg1.txt"),
                       std::ios::binary);
    REQUIRE(file);
    const std::string stream{std::istreambuf_iterator<char>(file),
                             std::istreambuf_iterator<char>()};

    const auto lines = draw(stream);
    CHECK(lines.size() == 39);
    // A sentence the message drew in four pieces across four of its own lines.
    bool found = false;
    for (const auto& line : lines) {
        if (line.text.find("This is an private system") != std::string::npos)
            found = true;
    }
    CHECK(found);
    for (const auto& line : lines) {
        CHECK(amberedit::ui::displayWidth(line.text) <= ansi::kColumns);
    }
}

TEST_CASE("the sample artwork replays onto the canvas [ansi]") {
    // The one end-to-end case: a real ANSI file as a BBS wrote it, drawn as
    // chunks that undo their own newline with an ESC[A and count on a border in
    // column 80 wrapping out of the way. Nothing here holds unless the whole
    // replay does.
    std::ifstream file(amberedit::test::projectPath("testdata/ansi/ansi_msg.txt"),
                       std::ios::binary);
    REQUIRE(file);
    const std::string stream{std::istreambuf_iterator<char>(file),
                             std::istreambuf_iterator<char>()};
    REQUIRE(ansi::containsCodes(stream));

    const auto lines = draw(stream);
    // 96 lines of file, 39 rows of picture: the newlines are not the rows.
    CHECK(lines.size() == 39);
    CHECK(lines.front().text == "           ▀▀█▒▒░▄▄▄  ▀▄▄▄");
    // Nothing is wider than the canvas, which is what says the wrap happened.
    for (const auto& line : lines) {
        CHECK(amberedit::ui::displayWidth(line.text) <= ansi::kColumns);
    }
    // The text drawn into the middle of the picture, which only lands in one
    // piece if the cursor was where the message left it on every chunk.
    bool found = false;
    for (const auto& line : lines) {
        if (line.text.find("telnet>>20ForBeers.com:1337") != std::string::npos)
            found = true;
    }
    CHECK(found);
}
