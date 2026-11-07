#pragma once

#include "ui/wheel_throttle.hpp"

/// Telling one click from a double and a triple one. ncurses is not asked:
/// `mouseinterval(0)` keeps it from holding a press back to see whether a
/// second follows, so it never reports one, and the screens count for
/// themselves — the editor and the reader's selection mode, where a second
/// click selects a word and a third the line.
namespace amberedit::ui {

/// The run of presses so far: where the last one landed, when, and how many
/// have come there in a row.
struct ClickRun {
    int x{-1};
    int y{-1};
    Millis at{0};
    int count{0};
};

/// How close together two presses have to come to be one run. The common
/// desktop default; nothing behind the number is a user's to prefer.
constexpr Millis kMultiClickMs = 400;

/// Counts a press at (`x`, `y`) into `run` and answers which of a run it is: 1,
/// 2 or 3. A press on another cell, or later than `kMultiClickMs` after the one
/// before, begins a run of its own, and so does the one after a triple.
inline int countClick(ClickRun& run, int x, int y, Millis now) {
    const bool continues =
        run.count > 0 && run.count < 3 && x == run.x && y == run.y && now - run.at <= kMultiClickMs;
    run.count = continues ? run.count + 1 : 1;
    run.x = x;
    run.y = y;
    run.at = now;
    return run.count;
}

}  // namespace amberedit::ui
