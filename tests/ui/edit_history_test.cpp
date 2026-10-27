#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "ui/edit_history.hpp"

using amberedit::ui::EditContent;
using amberedit::ui::EditFocus;
using amberedit::ui::EditHistory;

namespace {

using Lines = std::vector<std::string>;
using Kind = EditHistory::Kind;

/// The typing in the text at a line and a column.
EditFocus at(int row, size_t col) {
    EditFocus focus;
    focus.text = {row, col};
    return focus;
}

const Lines kHeader{"From", "", "To", "", "Subject"};

}  // namespace

TEST_CASE("A step keeps the lines that changed and puts them back [undo]") {
    EditHistory history;
    history.settle({"a", "b", "c"}, kHeader, at(1, 1), Kind::Other);
    history.settle({"a", "b1", "b2", "c"}, kHeader, at(2, 0), Kind::Other);
    history.settle({"a", "b1", "b2"}, kHeader, at(2, 0), Kind::Other);

    EditContent content{{"a", "b1", "b2"}, kHeader, at(2, 0)};
    REQUIRE(history.undo(content));
    CHECK(content.lines == Lines{"a", "b1", "b2", "c"});
    CHECK(content.focus == at(2, 0));
    REQUIRE(history.undo(content));
    CHECK(content.lines == Lines{"a", "b", "c"});
    CHECK(content.focus == at(1, 1));
    CHECK_FALSE(history.undo(content));

    REQUIRE(history.redo(content));
    REQUIRE(history.redo(content));
    CHECK(content.lines == Lines{"a", "b1", "b2"});
    CHECK_FALSE(history.redo(content));
}

TEST_CASE("Typing in a run is one step, broken where a word ends [undo]") {
    EditHistory history;
    history.settle({""}, kHeader, at(0, 0), Kind::Other);
    history.settle({"h"}, kHeader, at(0, 1), Kind::Typing, "h");
    history.settle({"hi"}, kHeader, at(0, 2), Kind::Typing, "i");
    history.settle({"hi "}, kHeader, at(0, 3), Kind::Typing, " ");
    history.settle({"hi  "}, kHeader, at(0, 4), Kind::Typing, " ");
    history.settle({"hi  x"}, kHeader, at(0, 5), Kind::Typing, "x");

    EditContent content{{"hi  x"}, kHeader, at(0, 5)};
    REQUIRE(history.undo(content));
    CHECK(content.lines == Lines{"hi"});
    REQUIRE(history.undo(content));
    CHECK(content.lines == Lines{""});
}

TEST_CASE("A cursor moved between two keystrokes ends the run [undo]") {
    EditHistory history;
    history.settle({"ab"}, kHeader, at(0, 2), Kind::Other);
    history.settle({"abc"}, kHeader, at(0, 3), Kind::Typing, "c");
    history.settle({"abc"}, kHeader, at(0, 0), Kind::Other);
    history.settle({"xabc"}, kHeader, at(0, 1), Kind::Typing, "x");

    EditContent content{{"xabc"}, kHeader, at(0, 1)};
    REQUIRE(history.undo(content));
    CHECK(content.lines == Lines{"abc"});
    CHECK(content.focus == at(0, 0));
}

TEST_CASE("A header field is a step of its own, and comes back with the typing [undo]") {
    EditFocus subject;
    subject.inHeader = true;
    subject.field = 4;
    subject.fieldCursor = 7;

    EditHistory history;
    history.settle({"text"}, kHeader, subject, Kind::Other);
    Lines changed = kHeader;
    changed[4] = "Subjects";
    EditFocus typed = subject;
    typed.fieldCursor = 8;
    history.settle({"text"}, changed, typed, Kind::Typing, "s");

    EditContent content{{"text"}, changed, typed};
    REQUIRE(history.undo(content));
    CHECK(content.header == kHeader);
    CHECK(content.lines == Lines{"text"});
    CHECK(content.focus == subject);
}
