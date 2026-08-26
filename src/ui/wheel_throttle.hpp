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

/// How quiet the wheel has to go for the flick a change of what is in front of
/// the user ended to be over: two notches further apart than this are two movements of
/// the wheel and not one. The same span as `list_wheel_throttle_ms` defaults to, and
/// deliberately not that setting — turning the counting of notches off says
/// nothing about a tail landing on the wrong screen, and this is not a length
/// anybody has a reason to tune.
inline constexpr Millis kWheelSettleMs = 200;

/// Keeps what is left of a flick of the wheel off whatever comes next.
///
/// A notch arrives long after the hand that asked for it: a trackpad goes on
/// reporting them once the finger has left it, and a wheel turned faster than
/// the frames can be drawn leaves them waiting to be read. So the notches still
/// coming when Escape closes the reader were aimed at the message it closed, and
/// answering them runs the list underneath to its end under a hand that has
/// already stopped. Entering an area is the same the other way about, and so is
/// a box opening over the reader: the tail scrolls the list inside it.
///
/// A change of what the wheel is aimed at therefore ends whatever run was in
/// flight: its notches are swallowed for as long as they keep arriving, and the
/// wheel is live again on the first notch that comes after a gap of `settleMs`,
/// on the first one turning the other way, or once `capMs` — `wheel_settle_ms`
/// — has passed since the change. A fresh flick begun inside that window is
/// lost with the tail — nothing the terminal reports says which of the two a
/// notch belongs to — which is why the window is as short as it is.
struct WheelSettle {
    /// Which way the run so far is going, -1 up and +1 down, and 0 before the
    /// wheel has been touched at all.
    int direction{0};
    /// When the last notch arrived, swallowed or not: what a gap tells apart is
    /// a tail from a hand, and a swallowed notch is as much part of a tail as an
    /// answered one.
    Millis last{0};
    /// When what the wheel is aimed at last changed.
    Millis since{0};
    /// Whether a run left over from what it was aimed at before may still be
    /// arriving.
    bool blocking{false};

    /// Something else is in front of the user as of `now` — another screen, or
    /// a box opened over one, or the box that has taken another's place.
    void focusChanged(Millis now) {
        blocking = true;
        since = now;
    }

    /// Whether this notch belongs to the flick that change ended, and is to be
    /// answered by nothing at all. `delta` is what `wheelDelta()` read off the
    /// event, so 0 — anything that is not a notch — is never swallowed.
    bool swallows(int delta, Millis now, Millis settleMs, Millis capMs) {
        if (delta == 0) return false;
        // `wheel_settle_ms 0` is the guard turned off: nothing is swallowed,
        // and the run being swallowed when it goes off ends there — the notches
        // of it left are the wheel, and the setting says to answer them.
        // `direction == delta` also settles the case of a change coming before
        // the wheel has been touched: direction is 0 then, and no notch matches.
        const bool leftOver = capMs > 0 && blocking && direction == delta &&
                              now - last <= settleMs && now - since <= capMs;
        direction = delta;
        last = now;
        blocking = leftOver;
        return leftOver;
    }
};

}  // namespace amberedit::ui
