#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "app/area_manager.hpp"
#include "config/app_config.hpp"
#include "test_strings.hpp"
#include "ui/area_list_format.hpp"

using amberedit::app::AreaEntry;
using amberedit::config::AppConfig;
using amberedit::config::AreaFieldKind;
using amberedit::config::AreaListFormat;

namespace area_format = amberedit::ui::area_format;

namespace {

/// The fields a format string asks for, read through the config so that the
/// test lays out what a user would actually have written.
AreaListFormat fields(const std::string& format) {
    return amberedit::test::valueOf(
               AppConfig::loadFromString("tosser_config a\ntosser_config_format hpt\n"
                                         "default_charset CP866\ncompose_charset CP866\n"
                                         "arealist_format \"" +
                                         format + "\"\n"))
        .areaListFormatNarrow;
}

/// The one line a format written without `\n` in it lays out — what most of
/// these tests are about, a row being one line unless it says otherwise.
area_format::Line line(const std::string& format, int width) {
    return area_format::layout(fields(format), width).front();
}

AreaEntry areaEntry(const std::string& tag, uint32_t total, uint32_t unread) {
    AreaEntry entry;
    entry.config.tag = tag;
    entry.total = total;
    entry.unread = unread;
    return entry;
}

std::string headerOf(const std::string& format, int width) {
    return area_format::header(area_format::layout(fields(format), width));
}

/// `descriptionDefault` stands where the config's would: what the area list
/// shows for an area nothing describes, the config's own default unless a test
/// says otherwise.
std::string rowOf(const std::string& format, int width, const AreaEntry& entry,
                  int ordinal = 1,
                  const std::string& descriptionDefault = "no description") {
    return area_format::row(entry, ordinal, line(format, width), descriptionDefault);
}

}  // namespace

TEST_CASE("The area list's flexible fields share what the fixed ones leave "
          "[arealist][format]") {
    // "e c un": 4 + 4 + 1 for the counts and the star, two spaces between them,
    // so the name takes the other fourteen of the twenty-five.
    const auto columns = line("e c un", 25);
    REQUIRE(columns.size() == 6);
    CHECK(columns[0].width == 14);
    CHECK(columns[1].width == 1);
    CHECK(columns[2].width == 4);
    CHECK(columns[4].width == 4);
    CHECK(columns[5].width == 1);

    // Two fields written 0 halve what is left, and the odd column goes to the
    // first of them.
    const auto shared = line("e d c", 21);
    REQUIRE(shared.size() == 5);
    CHECK(shared[0].width == 8);
    CHECK(shared[2].width == 7);
    CHECK(shared[4].width == 4);

    // A window narrower than the fixed widths leaves the flexible fields
    // nothing rather than taking the room from the fixed ones.
    const auto cramped = line("e c un", 5);
    CHECK(cramped[0].width == 0);
}

TEST_CASE("Each line of a multi-line format is laid out on its own "
          "[arealist][format]") {
    // A row of two lines is two layouts: the counts are on the first line, so
    // the name has what they leave of the twenty-five, and the description on
    // the second has the whole of it but the star and the gap.
    const auto rows = area_format::layout(fields("e c u\\nd n"), 25);
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].size() == 5);
    CHECK(rows[0][0].kind == AreaFieldKind::Echoid);
    CHECK(rows[0][0].width == 15);
    REQUIRE(rows[1].size() == 3);
    CHECK(rows[1][0].kind == AreaFieldKind::Description);
    CHECK(rows[1][0].width == 23);

    // A line whose own fields all have widths gives nothing away to the line
    // above or below: what is left over on one is not shared with the other.
    const auto fixedAndFlexible = area_format::layout(fields("a e\\nd"), 20);
    REQUIRE(fixedAndFlexible.size() == 2);
    CHECK(fixedAndFlexible[0][2].width == 15);
    CHECK(fixedAndFlexible[1][0].width == 20);
}

TEST_CASE("The area list's row for a line of the format is that line alone "
          "[arealist][format]") {
    const AreaEntry entry = areaEntry("ru.linux", 120, 7);
    const auto rows = area_format::layout(fields("e c u\\nd n"), 25);

    CHECK(area_format::row(entry, 1, rows[0], "no description") ==
          "ru.linux         120    7");
    CHECK(area_format::row(entry, 1, rows[1], "no description") ==
          "no description          *");
}

TEST_CASE("The area list's heading stands over the format's own columns "
          "[arealist][format]") {
    CHECK(headerOf("e c un", 25) == "Area           Msgs  New ");
    // A heading is cut to its column rather than truncated with an ellipsis:
    // the column is narrow enough already.
    CHECK(headerOf("c3 u3", 8) == "Msg New");
    // A row several lines tall still has the one heading row, and it stands
    // over the line the row is read from first. What is under that line is read
    // by what it holds — a second row of furniture would cost the list an area.
    CHECK(headerOf("e c u\\nd n", 25) == "Area            Msgs  New");
}

TEST_CASE("An area list row is laid out by the format [arealist][format]") {
    const AreaEntry entry = areaEntry("ru.linux", 120, 7);

    CHECK(rowOf("e c un", 25, entry) == "ru.linux        120    7*");
    // Nothing unread leaves the star's column blank rather than filling it.
    CHECK(rowOf("e c un", 25, areaEntry("ru.linux", 120, 0)) ==
          "ru.linux        120    0 ");
    // The ordinal is the row's place in the list as it stands.
    CHECK(rowOf("a e", 20, entry, 12) == "  12 ru.linux       ");
    // A name too long for its column is truncated, the ellipsis saying so.
    CHECK(rowOf("e8 c4", 13, areaEntry("ru.comp.os.linux", 5, 0)) == "ru.comp…    5");
}

TEST_CASE("An area nothing describes shows the default description "
          "[arealist][format]") {
    AreaEntry entry = areaEntry("ru.linux", 3, 0);

    // Nothing said about the area anywhere: the column stands in with what
    // `arealist_description_default` gives it.
    CHECK(rowOf("e8 d", 25, entry) == "ru.linux no description  ");
    // A description of its own is untouched by the setting.
    entry.config.description = "Linux";
    CHECK(rowOf("e8 d", 25, entry) == "ru.linux Linux           ");
    // Empty is the old behaviour asked for by name: the column stays blank.
    entry.config.description.clear();
    CHECK(rowOf("e8 d", 25, entry, 1, "") == "ru.linux                 ");
    // The stand-in is cut to its column exactly as a description is.
    CHECK(rowOf("e8 d4", 13, entry, 1, "no idea") == "ru.linux no …");
}

TEST_CASE("An area that would not open shows no counts [arealist][format]") {
    AreaEntry broken = areaEntry("ru.broken", 0, 0);
    broken.error = "no such base";

    CHECK(rowOf("e c un", 25, broken) == "ru.broken         —    — ");
}

TEST_CASE("A count too wide for its column is shortened, not cut [arealist][format]") {
    CHECK(area_format::countText(120, 4) == "120");
    CHECK(area_format::countText(1234, 4) == "1234");

    // Thousands and millions, the digits below the suffix dropped rather than
    // rounded — 17342 messages is 17k and not 18k.
    CHECK(area_format::countText(17342, 3) == "17k");
    CHECK(area_format::countText(1034556, 4) == "1M");
    CHECK(area_format::countText(1034556, 5) == "1034k");
    CHECK(area_format::countText(3000000000, 4) == "3G");

    // Where not even that fits, the column says there is something there.
    CHECK(area_format::countText(999, 1) == "+");
    CHECK(area_format::countText(17342, 2) == "+");
    CHECK(area_format::countText(9, 1) == "9");
    // A column of no width is not drawn at all.
    CHECK(area_format::countText(120, 0).empty());
}
