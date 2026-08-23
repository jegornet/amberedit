#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "config/app_config.hpp"
#include "domain/message.hpp"
#include "test_strings.hpp"
#include "ui/msg_list_format.hpp"

using amberedit::config::AppConfig;
using amberedit::config::MsgFieldKind;
using amberedit::config::MsgListFormat;
using amberedit::domain::MessageHeader;

namespace msg_format = amberedit::ui::msg_format;

namespace {

/// The fields a format string asks for, read through the config so that the
/// test lays out what a user would actually have written.
MsgListFormat fields(const std::string& format) {
    return amberedit::test::valueOf(
               AppConfig::loadFromString("tosser_config a\ntosser_config_format hpt\n"
                                         "default_charset CP866\ncompose_charset CP866\n"
                                         "name Vasya Pupkin\naddress 2:5020/9999.1\n"
                                         "msglist_format \"" +
                                         format + "\"\n"))
        .messageListFormatNarrow;
}

MessageHeader message(const std::string& from, const std::string& to,
                      const std::string& subject) {
    MessageHeader header;
    header.from = from;
    header.to = to;
    header.subject = subject;
    return header;
}

/// One row of the table, stamp and all. The stamp is handed over as text rather
/// than made from a date: what the Date column does with it is the same either
/// way, and a literal says what the test is measuring.
msg_format::Row row(const MessageHeader& header, int number, std::string stamp = {}) {
    msg_format::Row drawn;
    drawn.header = &header;
    drawn.number = number;
    drawn.stamp = std::move(stamp);
    return drawn;
}

/// The one line a format written without `\n` in it lays out.
msg_format::Line line(const std::string& format, int width, uint32_t messageCount,
                      const std::vector<msg_format::Row>& shown) {
    return msg_format::layout(fields(format), width, messageCount, shown).front();
}

}  // namespace

TEST_CASE("The message list's number column is as wide as the numbers in it "
          "[msglist][format]") {
    const MessageHeader header = message("Vasya", "All", "Hello");
    const std::vector<msg_format::Row> shown{row(header, 1)};

    // No width written after the letter: the column stands as wide as the
    // highest number that can go in it, and the subject takes the rest.
    CHECK(line("a s", 40, 1200, shown)[0].width == 4);
    CHECK(line("a s", 40, 150402, shown)[0].width == 6);
    // Never under three, however few messages the area holds: a handful of them
    // should read as a column rather than as a stray digit against the edge.
    CHECK(line("a s", 40, 7, shown)[0].width == 3);
    CHECK(line("a s", 40, 0, shown)[0].width == 3);
    CHECK(line("a s", 40, 999, shown)[0].width == 3);
    CHECK(line("a s", 40, 1000, shown)[0].width == 4);
    // And the field beside it has that much less to work with.
    CHECK(line("a s", 40, 1200, shown)[2].width == 40 - 4 - 1);

    // A width written after the letter is the width, whatever the area holds.
    CHECK(line("a8 s", 40, 150402, shown)[0].width == 8);
    // Written 0 it takes what is left over like any other flexible field: two
    // of them halve the thirty-nine the gap leaves, the first taking the odd
    // column.
    CHECK(line("a0 s", 40, 150402, shown)[0].width == 20);
    CHECK(line("a0 s", 40, 150402, shown)[2].width == 19);
}

TEST_CASE("The message list's Date column takes what the stamps need "
          "[msglist][format]") {
    const MessageHeader header = message("Vasya", "All", "Hello");
    const std::vector<msg_format::Row> shown{row(header, 1, "15 Aug 26 20:28"),
                                             row(header, 2, "9 Aug 26 07:05")};

    // Fifteen columns for the widest stamp on the screen, and the subject has
    // the rest: the Date column is settled before the fields written 0, so what
    // it does not use is theirs rather than five empty columns of stamp.
    const auto columns = line("a3 s d", 60, 999, shown);
    REQUIRE(columns.size() == 5);
    CHECK(columns[4].width == 15);
    CHECK(columns[2].width == 60 - 3 - 15 - 2);

    // The heading is a floor where there is room for it: an area whose stamps
    // are all shorter than "Date" still gets a column its heading fits in.
    const std::vector<msg_format::Row> brief{row(header, 1, "15")};
    CHECK(line("a3 s d", 60, 999, brief)[4].width == 4);
    // With no rows to measure at all — an area at its end, or headers not read
    // yet — the heading is the whole of what there is to go on.
    CHECK(line("a3 s d", 60, 999, {})[4].width == 4);

    // A window with less room than the stamps want does not hand the Date all
    // of it: the stamp is cut at the spaces, so the column takes what the cut
    // one comes to — "15 Aug" of the seven columns free here — and the subject
    // keeps what that leaves rather than the five empty columns of a stamp that
    // was never going to fit.
    const auto cramped = line("a3 s d", 12, 999, shown);
    CHECK(cramped[4].width == 6);
    CHECK(cramped[2].width == 1);

    // A width written after the letter is the width: the stamp is then cut to
    // it rather than the column cut to the stamp.
    CHECK(line("a3 s d9", 60, 999, shown)[4].width == 9);
}

TEST_CASE("A stamp too wide for its column drops its trailing parts "
          "[msglist][format]") {
    // At the spaces and never mid-word: a stamp cut mid-word would read as a
    // different date.
    const std::string full = "15 Aug 26 20:28 +0200";
    CHECK(msg_format::fitDate(full, 21) == full);
    CHECK(msg_format::fitDate(full, 20) == "15 Aug 26 20:28");
    CHECK(msg_format::fitDate(full, 15) == "15 Aug 26 20:28");
    CHECK(msg_format::fitDate(full, 14) == "15 Aug 26");
    CHECK(msg_format::fitDate(full, 8) == "15 Aug");
    CHECK(msg_format::fitDate(full, 5) == "15");
    // Only when even the first part will not fit is the stamp cut where the
    // width falls, and then it is truncated as any text is — the ellipsis says
    // the column has a date in it and no room to show one.
    CHECK(msg_format::fitDate(full, 1) == "…");
    CHECK(msg_format::fitDate(full, 0).empty());
}

TEST_CASE("Each line of a multi-line message format is laid out on its own "
          "[msglist][format]") {
    const MessageHeader header = message("Vasya", "All", "Hello");
    const std::vector<msg_format::Row> shown{row(header, 1, "15 Aug 26 20:28")};

    // The default narrow row: the number, the two names and the stamp on one
    // line, the subject across the whole of the next.
    const auto rows = msg_format::layout(fields("a f0 t0 d15\\ns"), 50, 999, shown);
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].size() == 7);
    CHECK(rows[0][0].width == 3);   // the numbers of an area of 999
    CHECK(rows[0][6].width == 15);  // the stamp, at the width the format wrote
    // What the number, the stamp and the three gaps leave, halved, the first
    // name taking the odd column.
    CHECK(rows[0][2].width == 15);
    CHECK(rows[0][4].width == 14);
    // The second line is measured on its own: nothing above it is spent there.
    REQUIRE(rows[1].size() == 1);
    CHECK(rows[1][0].width == 50);
}

TEST_CASE("The message list's heading stands over the format's own columns "
          "[msglist][format]") {
    const MessageHeader header = message("Vasya", "All", "Hello");
    const std::vector<msg_format::Row> shown{row(header, 1, "15 Aug 26 20:28")};

    // The heading follows the format — no To asked for, none drawn — and the
    // number's stands against the right edge of its column as the numbers do.
    CHECK(msg_format::header(msg_format::layout(fields("a4 f8 s d"), 40, 999, shown)) ==
          "   # From     Subject    Date           ");
    // A row several lines tall still has the one heading row, over the line the
    // row is read from first.
    CHECK(msg_format::header(msg_format::layout(fields("a4 f8\\ns"), 20, 999, shown)) ==
          "   # From    ");
}

TEST_CASE("A message list row is laid out by the format [msglist][format]") {
    const MessageHeader header = message("Vasya Pupkin", "All", "About the weather");
    const auto columns = line("a4 f8 s d", 40, 999, {row(header, 12, "15 Aug 26")});

    CHECK(msg_format::line(row(header, 12, "15 Aug 26"), columns) ==
          "  12 Vasya P… About the weath… 15 Aug 26");
}

TEST_CASE("The subject is the run of a message row that is drawn quiet "
          "[msglist][format]") {
    using amberedit::ui::msg_format::Ink;
    const MessageHeader header = message("Vasya", "All", "Hello");

    msg_format::Row drawn = row(header, 1, "15 Aug 26");
    const auto columns = line("a3 f8 t8 s d", 60, 999, {drawn});

    // The subject is prose rather than a fact about the message, so it is cut
    // out of the row as a run of its own; everything plain around it stays the
    // one run the whole line used to be.
    auto runs = msg_format::runs(drawn, columns);
    REQUIRE(runs.size() == 3);
    CHECK(runs[0].ink == Ink::Plain);
    CHECK(runs[1].ink == Ink::Dimmed);
    CHECK(runs[1].text.find("Hello") != std::string::npos);
    CHECK(runs[2].ink == Ink::Plain);

    // A From that names the user is a run of its own too, and on the same
    // terms — the screen is what knows which name that is.
    drawn.fromIsOwn = true;
    runs = msg_format::runs(drawn, columns);
    REQUIRE(runs.size() == 5);
    CHECK(runs[0].ink == Ink::Plain);
    CHECK(runs[1].ink == Ink::OwnName);
    CHECK(runs[1].text.find("Vasya") != std::string::npos);
    CHECK(runs[2].ink == Ink::Plain);
    CHECK(runs[3].ink == Ink::Dimmed);
    CHECK(runs[4].ink == Ink::Plain);
}

TEST_CASE("A message row with no header read yet is drawn blank [msglist][format]") {
    const MessageHeader header = message("Vasya", "All", "Hello");
    const auto columns = line("a3 f8 s d", 40, 999, {row(header, 1, "15 Aug 26")});

    // The window has not reached it: the columns are there and empty rather
    // than the row being a different width from the ones around it.
    msg_format::Row pending;
    pending.number = 4;
    const std::string drawn = msg_format::line(pending, columns);
    CHECK(drawn.find_first_not_of(' ') == std::string::npos);
    CHECK(msg_format::line(pending, columns).size() ==
          msg_format::line(row(header, 4, "15 Aug 26"), columns).size());
}
