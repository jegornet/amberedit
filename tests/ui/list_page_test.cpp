#include <catch2/catch.hpp>

#include "ui/list_page.hpp"

using amberedit::ui::pageDownTarget;
using amberedit::ui::pageUpTarget;

// The window throughout: 28 rows of a 58-row list.

TEST_CASE("PageUp stops at the top visible row before turning the page",
          "[listpage]") {
    // A cursor partway down the window — row 39 of 30..57 — goes to the top
    // row first: the rows above it are on the screen already.
    CHECK(pageUpTarget(39, 30, 28) == 30);
    // Only from the top row does the page turn, one row short of the window,
    // so the row that was on top stays in view at the bottom.
    CHECK(pageUpTarget(30, 30, 28) == 3);
    // Near the start the page runs past the list; the caller's clamping is
    // what stops the cursor at the first row.
    CHECK(pageUpTarget(3, 3, 28) == -24);
}

TEST_CASE("PageDown stops at the bottom visible row before turning the page",
          "[listpage]") {
    CHECK(pageDownTarget(5, 0, 28, 58) == 27);
    CHECK(pageDownTarget(27, 0, 28, 58) == 54);
    // A list ending above the window's last row puts its own last row where
    // the window's would be.
    CHECK(pageDownTarget(50, 40, 28, 58) == 57);
}

TEST_CASE("A one-row window pages a row at a time", "[listpage]") {
    // The cursor is always the top and the bottom row at once there, and a
    // page a row short of that window would be no movement at all.
    CHECK(pageUpTarget(5, 5, 1) == 4);
    CHECK(pageDownTarget(5, 5, 1, 10) == 6);
}
