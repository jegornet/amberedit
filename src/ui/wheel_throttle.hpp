#pragma once

#include <cstdint>

namespace amberedit::ui {

/// A moment read off a monotonic clock, in milliseconds. Monotonic and not the
/// wall clock: what is measured is the gap between two notches of the wheel, and
/// a clock that can be set backwards would turn one such gap into a negative
/// number.
using Millis = std::int64_t;

/// Spends a row's worth of wheel notches on each row of a list whose rows stand
/// more than one line tall.
///
/// A wheel notch is a line, and every terminal sends the same one whatever is
/// under the pointer. Where a row of the list is two or three lines tall, taking
/// each notch for a row moves the list two or three times as fast as the same
/// wheel moves anything else on the screen — the list runs away under a flick
/// that would have moved a message a few lines. So the notches are counted: the
/// first of a run moves the cursor and the rest of that row's worth are
/// swallowed, which brings a row of the list back to a row's worth of wheel.
///
/// A run is notches in the one direction arriving no further apart than
/// `throttleMs`. Anything slower is somebody scrolling by hand rather than
/// flicking, and every notch of it moves the cursor: the throttle is there to
/// make a fast wheel proportionate, not to make a deliberate one wait. Turning
/// back moves at once for the same reason — the direction is the user changing
/// their mind, and there is nothing to spend on a row they are leaving.
struct WheelThrottle {
    /// Which way the run so far is going, -1 up and +1 down, and 0 before there
    /// has been one.
    int direction{0};
    /// How many notches of the current row's worth have been swallowed already.
    int skipped{0};
    /// When the last notch arrived — accepted or swallowed, since what the gap
    /// tells apart is a flick from a hand, and a swallowed notch is as much part
    /// of a flick as an accepted one.
    Millis last{0};

    /// What one notch moves the cursor by: `delta` where it counts, and 0 where
    /// this notch is one of the ones spent on the row already moved onto.
    ///
    /// `rowHeight` is how many lines a row of the list stands, so a single-line
    /// list — which is every list drawn from a format with no `\n` in it — is
    /// handed straight back its notch. So is every notch when `throttleMs` is
    /// zero, which is how the setting is turned off from the config.
    int step(int delta, int rowHeight, Millis now, int throttleMs) {
        if (delta == 0) return 0;
        const bool sameRun = direction == delta && now - last <= throttleMs;
        direction = delta;
        last = now;
        if (rowHeight <= 1 || throttleMs <= 0 || !sameRun) {
            skipped = 0;
            return delta;
        }
        // The row moved onto still owes the lines under its first one. Only once
        // they have been paid does a notch move the cursor again, which is what
        // makes a two-line row cost two notches and a three-line row three.
        if (skipped + 1 < rowHeight) {
            ++skipped;
            return 0;
        }
        skipped = 0;
        return delta;
    }
};

}  // namespace amberedit::ui
