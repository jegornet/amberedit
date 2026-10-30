#include <doctest/doctest.h>

#include "config/app_config.hpp"
#include "ui/header_format.hpp"

using amberedit::config::HeaderField;
using amberedit::config::HeaderFieldKind;
using amberedit::config::HeaderFormat;
using amberedit::config::HeaderLabel;
using amberedit::config::kAutoWidth;

namespace header_format = amberedit::ui::header_format;

namespace {

/// A stamp measured at `width`, whatever it shows.
header_format::Measure stampsOf(int width) {
    return [width](const HeaderField&) { return width; };
}

}  // namespace

TEST_CASE("A line is laid out as a list's is [header_format]") {
    // A literal as wide as it is written, a label at its width, then the
    // shares: two fields written 0 split 21 columns, the odd one to the first.
    const HeaderFormat format{{{HeaderFieldKind::Label, 5, {}, HeaderLabel::From},
                               {HeaderFieldKind::Literal, 0, ": "},
                               {HeaderFieldKind::FromName, 0},
                               {HeaderFieldKind::FromAddr, 0},
                               {HeaderFieldKind::Attributes, 2}}};
    const auto layout = header_format::layout(format, 30, stampsOf(0));
    REQUIRE(layout.size() == 1);
    const auto& line = layout.front();
    CHECK(line[0].width == 5);
    CHECK(line[1].width == 2);
    CHECK(line[2].x == 7);
    CHECK(line[2].width == 11);
    CHECK(line[3].x == 18);
    CHECK(line[3].width == 10);
    CHECK(line[4].x == 28);
}

TEST_CASE(
    "A label written with no width stands at the widest label's "
    "[header_format]") {
    const HeaderFormat format{
        {{HeaderFieldKind::Label, kAutoWidth, {}, HeaderLabel::To}}};
    const auto layout = header_format::layout(format, 80, stampsOf(0));
    CHECK(layout.front().front().width == header_format::labelWidth());
    CHECK(header_format::labelWidth() >= 4);
}

TEST_CASE(
    "A stamp is measured, and held to its share of what is left "
    "[header_format]") {
    const HeaderFormat format{{{HeaderFieldKind::Written, kAutoWidth},
                               {HeaderFieldKind::Arrived, kAutoWidth},
                               {HeaderFieldKind::Subject, 0}}};
    // Room for both: each takes what it comes to, the subject the rest.
    auto line = header_format::layout(format, 60, stampsOf(15)).front();
    CHECK(line[0].width == 15);
    CHECK(line[1].width == 15);
    CHECK(line[2].width == 30);
    // Room for neither: an even share each, and nothing for the subject.
    line = header_format::layout(format, 20, stampsOf(15)).front();
    CHECK(line[0].width == 10);
    CHECK(line[1].width == 10);
    CHECK(line[2].width == 0);
}

TEST_CASE("A cap holds only a field with nothing typed after it [header_format]") {
    const HeaderFormat format{
        {{HeaderFieldKind::FromName, 0}, {HeaderFieldKind::FromAddr, 10}},
        {{HeaderFieldKind::Subject, 0}, {HeaderFieldKind::Literal, 0, "|"}},
    };
    const header_format::Cap cap = [](HeaderFieldKind) { return 20; };
    const auto layout = header_format::layout(format, 100, stampsOf(0), cap);
    // The name has an address after it, which would move if it were held.
    CHECK(layout[0][0].width == 90);
    // The subject has only a literal after it.
    CHECK(layout[1][0].width == 20);
    CHECK(layout[1][1].x == 20);
}

TEST_CASE(
    "The location stands under the second field of the sender's line "
    "[header_format]") {
    HeaderFormat format{
        {{HeaderFieldKind::Written, 10}, {HeaderFieldKind::Attributes, 10}},
        {{HeaderFieldKind::Literal, 0, "  "},
         {HeaderFieldKind::FromName, 12},
         {HeaderFieldKind::FromAddr, 10}},
    };
    CHECK(header_format::locationColumn(header_format::layout(format, 80, stampsOf(0))) ==
          14);
    // A line holding the sender alone has no second column, and nothing comes
    // back: the rule puts the location at its right end.
    format[1].pop_back();
    CHECK_FALSE(
        header_format::locationColumn(header_format::layout(format, 80, stampsOf(0))));
}

TEST_CASE("What runs past the edge is cut there, a cell at a time [header_format]") {
    const HeaderFormat format{{{HeaderFieldKind::Label, 5, {}, HeaderLabel::From},
                               {HeaderFieldKind::FromName, 36},
                               {HeaderFieldKind::FromAddr, 23}}};
    const auto line = header_format::layout(format, 30, stampsOf(0)).front();
    // The label whole, the name to the edge, and nothing left for the address.
    CHECK(line[0].width == 5);
    CHECK(line[1].width == 25);
    CHECK(line[2].width == 0);
}
