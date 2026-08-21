#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "ui/bbs_codes.hpp"
#include "ui/text_layout.hpp"

using amberedit::ui::wrapText;
namespace bbs = amberedit::ui::bbs;

namespace {

/// The runs of a line as "offset:fg/bg" — easier to read in a failure than a
/// struct, and it says in one string which half of the color each code set.
std::vector<std::string> runsOf(const std::vector<bbs::ColorRun>& runs) {
    std::vector<std::string> out;
    out.reserve(runs.size());
    for (const auto& run : runs) {
        out.push_back(std::to_string(run.begin) + ":" + std::to_string(run.color.fg) +
                      "/" + std::to_string(run.color.bg));
    }
    return out;
}

}  // namespace

TEST_CASE("stripRenegade takes the codes out of the text [bbs]") {
    const bbs::CodedLine coded = bbs::stripRenegade("|10hello |12world");
    CHECK(coded.text == "hello world");
    // Bright green, then bright red — the DOS numbering, not the terminal's, so
    // 10 is green and 12 is red rather than the other way round.
    CHECK(runsOf(coded.runs) == std::vector<std::string>{"0:10/-1", "6:9/-1"});
}

TEST_CASE("stripRenegade reads a foreground and a background apart [bbs]") {
    // The example from the format's own documentation: high intensity white on
    // a blue background.
    const bbs::CodedLine coded = bbs::stripRenegade("|15|17text");
    CHECK(coded.text == "text");
    // Two codes side by side are one change: white foreground, blue background.
    CHECK(runsOf(coded.runs) == std::vector<std::string>{"0:15/4"});
}

TEST_CASE("stripRenegade maps the DOS color order onto the terminal's [bbs]") {
    const auto colorOf = [](const std::string& code) {
        return bbs::stripRenegade(code + "x").runs.front().color;
    };
    CHECK(colorOf("|00").fg == 0);  // black
    CHECK(colorOf("|01").fg == 4);  // blue, which the terminal counts fourth
    CHECK(colorOf("|02").fg == 2);  // green
    CHECK(colorOf("|03").fg == 6);  // cyan
    CHECK(colorOf("|04").fg == 1);  // red
    CHECK(colorOf("|05").fg == 5);  // magenta
    CHECK(colorOf("|06").fg == 3);  // brown, the dim yellow
    CHECK(colorOf("|07").fg == 7);  // white
    CHECK(colorOf("|08").fg == 8);  // and the bright half, in the same order
    CHECK(colorOf("|15").fg == 15);

    CHECK(colorOf("|16").bg == 0);  // the backgrounds, from black
    CHECK(colorOf("|23").bg == 7);  // to white
    // The eight DOS drew as either a bright background or a blinking
    // foreground: taken as the background, which is what the code is named
    // after and what a terminal can show without flashing anything.
    CHECK(colorOf("|24").bg == 8);
    CHECK(colorOf("|31").bg == 15);
    // A code sets one half and says nothing about the other.
    CHECK(colorOf("|31").fg == -1);
    CHECK(colorOf("|15").bg == -1);
}

TEST_CASE("stripRenegade leaves a pipe that opens no code alone [bbs]") {
    const auto textOf = [](const std::string& line) {
        return bbs::stripRenegade(line).text;
    };
    CHECK(textOf("a|b") == "a|b");
    CHECK(textOf("|9 out of 10") == "|9 out of 10");
    CHECK(textOf("|32 and |99") == "|32 and |99");  // past the last code there is
    CHECK(textOf("ends with |") == "ends with |");
    CHECK(textOf("|1") == "|1");
    CHECK(bbs::stripRenegade("a|b").runs.empty());
}

TEST_CASE("A color reaches the end of its line and no further [bbs]") {
    // Each line is read on its own, so a code left open on the line before is
    // not something a line can be handed: the reader colors this one from the
    // theme until a code of its own says otherwise.
    CHECK(bbs::stripRenegade("still green").runs.empty());

    // A code at the very end colors nothing — there is no text after it on this
    // line, and the next line does not inherit it.
    const bbs::CodedLine ending = bbs::stripRenegade("then red|12");
    CHECK(ending.text == "then red");
    CHECK(ending.runs.empty());
}

TEST_CASE("runsForRows opens every wrapped row in its own color [bbs]") {
    const bbs::CodedLine coded = bbs::stripRenegade("|10aaa bbb |12ccc ddd");
    const std::vector<std::string> rows = wrapText(coded.text, 7);
    REQUIRE(rows == std::vector<std::string>{"aaa bbb", "ccc ddd"});

    const auto runs = bbs::runsForRows(coded, rows);
    REQUIRE(runs.size() == 2);
    CHECK(runsOf(runs[0]) == std::vector<std::string>{"0:10/-1"});
    // The second row is a continuation: the color the break fell under opens
    // it, at offset zero, with no code of its own on that row.
    CHECK(runsOf(runs[1]) == std::vector<std::string>{"0:9/-1"});
}

TEST_CASE("runsForRows keeps a run's offset within its own row [bbs]") {
    const bbs::CodedLine coded = bbs::stripRenegade("aaa bbb |12ccc ddd");
    const std::vector<std::string> rows = wrapText(coded.text, 11);
    REQUIRE(rows == std::vector<std::string>{"aaa bbb ccc", "ddd"});

    const auto runs = bbs::runsForRows(coded, rows);
    REQUIRE(runs.size() == 2);
    // Offset 8 of the line is offset 8 of the first row, which begins at 0.
    CHECK(runsOf(runs[0]) == std::vector<std::string>{"8:9/-1"});
    CHECK(runsOf(runs[1]) == std::vector<std::string>{"0:9/-1"});
}

TEST_CASE("runsForRows leaves an uncolored line alone [bbs]") {
    const bbs::CodedLine coded = bbs::stripRenegade("nothing to color here");
    const auto runs = bbs::runsForRows(coded, wrapText(coded.text, 10));
    for (const auto& row : runs) CHECK(row.empty());
}
