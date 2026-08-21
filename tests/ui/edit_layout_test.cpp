#include <catch2/catch.hpp>

#include <string>
#include <vector>

#include "ui/edit_layout.hpp"
#include "ui/text_editor.hpp"

using amberedit::ui::EditRow;
using amberedit::ui::layoutRows;
using amberedit::ui::moveByRows;
using amberedit::ui::rowOfCursor;
using amberedit::ui::TextBuffer;

namespace {

TextBuffer bufferOf(std::vector<std::string> lines, int row, size_t col) {
    TextBuffer buffer;
    buffer.lines = std::move(lines);
    buffer.row = row;
    buffer.col = col;
    return buffer;
}

/// The rows as the text on them, so that a layout can be read in one line. The
/// cursor is put at the start, where it asks nothing of the layout.
std::vector<std::string> shownRows(const std::vector<std::string>& lines, int width) {
    std::vector<std::string> out;
    for (const auto& row : layoutRows(bufferOf(lines, 0, 0), width)) {
        out.push_back(
            lines[static_cast<size_t>(row.line)].substr(row.begin, row.end - row.begin));
    }
    return out;
}

}  // namespace

TEST_CASE("A line that fits takes one row", "[editlayout]") {
    const std::vector<std::string> lines{"short", "", "also short"};
    const auto rows = layoutRows(bufferOf(lines, 0, 0), 20);
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].line == 0);
    CHECK(rows[1].line == 1);
    // An empty line has a row of its own: there is a cursor to put on it.
    CHECK(rows[1].begin == 0);
    CHECK(rows[1].end == 0);
    CHECK(rows[2].line == 2);
    CHECK(rows[2].end == lines[2].size());
}

TEST_CASE("A line wider than the window is shown over several rows", "[editlayout]") {
    const std::vector<std::string> lines{"aaa bbb ccc ddd"};
    // The blank a row breaks at stays on the row it closes: it draws nothing
    // against the right edge, and the bytes have to divide between the rows.
    CHECK(shownRows(lines, 7) == std::vector<std::string>{"aaa bbb ", "ccc ddd"});

    // Every byte of the line is on exactly one row, and nothing was inserted
    // between them — the line itself is untouched.
    std::string joined;
    for (const auto& row : layoutRows(bufferOf(lines, 0, 0), 7)) {
        joined += lines[0].substr(row.begin, row.end - row.begin);
    }
    CHECK(joined == lines[0]);
}

TEST_CASE("A word too long for the window is cut where the width falls", "[editlayout]") {
    const std::vector<std::string> lines{"aaaa bbbbbbbbbb"};
    CHECK(shownRows(lines, 6) == std::vector<std::string>{"aaaa ", "bbbbbb", "bbbb"});
}

TEST_CASE("A row is never cut through a character", "[editlayout]") {
    const std::vector<std::string> lines{"Съешь ещё этих мягких булок"};
    for (const auto& row : layoutRows(bufferOf(lines, 0, 0), 12)) {
        const std::string piece = lines[0].substr(row.begin, row.end - row.begin);
        // A continuation byte where a row begins would mean half a letter on
        // each of two rows.
        CHECK((static_cast<unsigned char>(piece.front()) & 0xC0u) != 0x80u);
    }
}

TEST_CASE("The cursor stands on the row its character is drawn on", "[editlayout]") {
    const std::vector<std::string> lines{"aaa bbb ccc ddd"};
    const auto rows = layoutRows(bufferOf(lines, 0, 0), 7);
    REQUIRE(rows.size() == 2);

    CHECK(rowOfCursor(rows, 0, 0) == 0);
    CHECK(rowOfCursor(rows, 0, 7) == 0);
    // On the break: the character there is the first of the row below, and the
    // cursor is drawn where it is.
    CHECK(rowOfCursor(rows, 0, 8) == 1);
    // At the end of the line the cursor stays where the typing goes on from.
    CHECK(rowOfCursor(rows, 0, lines[0].size()) == 1);
}

TEST_CASE("A line filling the window breaks to leave the cursor a column",
          "[editlayout]") {
    // Eleven characters in a window eleven wide, and the cursor at the end of
    // them: there is no twelfth column for it to stand in, so the last word
    // comes down onto a row of its own — which is what the eye expects of a
    // window one character short.
    const std::vector<std::string> lines{"aaa bbb ccc"};
    const auto rows = layoutRows(bufferOf(lines, 0, lines[0].size()), 11);
    REQUIRE(rows.size() == 2);
    CHECK(lines[0].substr(rows[0].begin, rows[0].end - rows[0].begin) == "aaa bbb ");
    CHECK(lines[0].substr(rows[1].begin, rows[1].end - rows[1].begin) == "ccc");
    CHECK(rowOfCursor(rows, 0, lines[0].size()) == 1);

    // The cursor anywhere else asks for nothing: the character it stands on is
    // drawn in a column of its own already, and the line fits as it is.
    CHECK(layoutRows(bufferOf(lines, 0, 4), 11).size() == 1);
}

TEST_CASE("A line broken at its very end leaves the cursor an empty row",
          "[editlayout]") {
    // Nothing to move down — the row ends where the line does — so the cursor
    // gets a row with no text on it, as every editor gives it.
    const std::vector<std::string> lines{"aaaa"};
    const auto rows = layoutRows(bufferOf(lines, 0, 4), 4);
    REQUIRE(rows.size() == 2);
    CHECK(rows[1].begin == 4);
    CHECK(rows[1].end == 4);
    CHECK(rowOfCursor(rows, 0, 4) == 1);
}

TEST_CASE("Up and down move by rows of the screen, not by lines", "[editlayout]") {
    const std::vector<std::string> lines{"aaa bbb ccc ddd", "second"};
    TextBuffer buffer = bufferOf(lines, 0, 2);
    const auto rows = layoutRows(bufferOf(lines, 0, 0), 7);

    // Down from the first row of a wrapped line lands on the second row of the
    // same line, in the column it was in — not on the line below it.
    moveByRows(buffer, rows, 1, 7);
    CHECK(buffer.row == 0);
    CHECK(buffer.col == 10);

    moveByRows(buffer, rows, 1, 7);
    CHECK(buffer.row == 1);
    CHECK(buffer.col == 2);

    moveByRows(buffer, rows, -1, 7);
    CHECK(buffer.row == 0);
    CHECK(buffer.col == 10);

    // Off the top and off the bottom the cursor stops on the row it is on.
    moveByRows(buffer, rows, -5, 7);
    CHECK(buffer.row == 0);
    CHECK(buffer.col == 2);
    moveByRows(buffer, rows, 9, 7);
    CHECK(buffer.row == 1);
}

TEST_CASE("Coming down onto a row the line goes on past stops inside it",
          "[editlayout]") {
    // The byte one past the end of a row is the first of the row below, so a
    // cursor put there would look like it had moved two rows rather than one.
    const std::vector<std::string> lines{"0123456789ab", "wxyz"};
    TextBuffer buffer = bufferOf(lines, 1, 4);
    const auto rows = layoutRows(bufferOf(lines, 0, 0), 4);
    REQUIRE(rows.size() == 4);

    // Up two rows, from the end of "wxyz" onto the middle row of the long line,
    // which the line goes on past: the cursor stops on its last character.
    moveByRows(buffer, rows, -2, 4);
    CHECK(buffer.row == 0);
    CHECK(buffer.col == 7);
    CHECK(rowOfCursor(rows, buffer.row, buffer.col) == 1);

    // The last row of a line does close it, so there the cursor may stand past
    // the end — that is where the typing carries on from.
    TextBuffer end = bufferOf(lines, 1, 4);
    moveByRows(end, rows, -1, 4);
    CHECK(end.row == 0);
    CHECK(end.col == lines[0].size());
}
