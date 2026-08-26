#include <doctest/doctest.h>

#include "ui/wheel_throttle.hpp"

using amberedit::ui::Millis;
using amberedit::ui::WheelSettle;
using amberedit::ui::WheelThrottle;

namespace {

/// The setting's own default throughout: two notches further apart than this
/// are two movements of the wheel rather than one.
constexpr int kThrottleMs = 200;

/// A flick: notches in the one direction, close enough together to be a run.
/// Hands back how far the cursor moved over the whole of it.
int flick(WheelThrottle& wheel, int notches, int rowHeight, int delta = 1,
          Millis from = 1000, Millis apart = 20) {
    int moved = 0;
    for (int i = 0; i < notches; ++i)
        moved += wheel.step(delta, rowHeight, from + i * apart, kThrottleMs);
    return moved;
}

}  // namespace

TEST_CASE("A single-line row moves on every notch [wheel]") {
    // Nothing to count: a row is a line, and the wheel is already moving the
    // list as far as it moves anything else on the screen.
    WheelThrottle wheel;
    CHECK(flick(wheel, 10, 1) == 10);
}

TEST_CASE("A two-line row costs two notches and a three-line row three [wheel]") {
    WheelThrottle two;
    CHECK(flick(two, 10, 2) == 5);

    WheelThrottle three;
    CHECK(flick(three, 9, 3) == 3);

    // The first notch of the run moves at once — what is swallowed is the rest
    // of that row's worth, not the row the user asked for.
    WheelThrottle first;
    CHECK(first.step(1, 3, 1000, kThrottleMs) == 1);
    CHECK(first.step(1, 3, 1020, kThrottleMs) == 0);
    CHECK(first.step(1, 3, 1040, kThrottleMs) == 0);
    CHECK(first.step(1, 3, 1060, kThrottleMs) == 1);
}

TEST_CASE("Notches further apart than the window each move a row [wheel]") {
    // Scrolling by hand rather than flicking: the throttle is there to make a
    // fast wheel proportionate, not to make a deliberate one wait.
    WheelThrottle wheel;
    CHECK(flick(wheel, 5, 3, 1, 1000, kThrottleMs + 1) == 5);

    // The window is measured from the last notch, swallowed or not, so a run
    // that keeps up stays one run however long it goes on.
    WheelThrottle steady;
    CHECK(flick(steady, 60, 2, 1, 1000, kThrottleMs) == 30);
}

TEST_CASE("Turning the wheel back moves at once [wheel]") {
    // The direction changing is the user changing their mind, and there is
    // nothing left to spend on the row they are leaving.
    WheelThrottle wheel;
    CHECK(wheel.step(1, 2, 1000, kThrottleMs) == 1);
    CHECK(wheel.step(1, 2, 1020, kThrottleMs) == 0);
    CHECK(wheel.step(-1, 2, 1040, kThrottleMs) == -1);
    // And the run counts again from there, upwards.
    CHECK(wheel.step(-1, 2, 1060, kThrottleMs) == 0);
    CHECK(wheel.step(-1, 2, 1080, kThrottleMs) == -1);
}

TEST_CASE("A window of zero counts nothing [wheel]") {
    // How `list_wheel_throttle_ms 0` reads: no gap for a notch to fall inside,
    // so no notch belongs to the run before it, however fast they arrive.
    WheelThrottle wheel;
    int moved = 0;
    for (int i = 0; i < 6; ++i) moved += wheel.step(1, 3, 1000 + i, 0);
    CHECK(moved == 6);
}

namespace {

/// The two spans the shell settles a run with — the gap that ends a run, and
/// `wheel_settle_ms` as it ships — named here so that a test reads as the wheel
/// rather than as arithmetic.
constexpr Millis kSettleMs = 200;
constexpr Millis kCapMs = 1500;

/// One notch through the guard: true where it was swallowed as the tail of the
/// flick that a screen changing, or a box opening over one, ended.
bool swallowed(WheelSettle& settle, int delta, Millis now) {
    return settle.swallows(delta, now, kSettleMs, kCapMs);
}

}  // namespace

TEST_CASE(
    "The tail of the flick a change in front of the user ended is swallowed [wheel]") {
    // Scrolling the message, then Escape: what the trackpad goes on sending is
    // aimed at the message, not at the list that has taken its place.
    WheelSettle settle;
    CHECK_FALSE(swallowed(settle, 1, 1000));
    CHECK_FALSE(swallowed(settle, 1, 1020));
    settle.focusChanged(1030);
    CHECK(swallowed(settle, 1, 1040));
    CHECK(swallowed(settle, 1, 1080));
    CHECK(swallowed(settle, 1, 1140));

    // And the wheel is live again on the first notch after the tail has died
    // out, the gap being what tells a hand from what a hand has left behind.
    CHECK_FALSE(swallowed(settle, 1, 1140 + kSettleMs + 1));
    CHECK_FALSE(swallowed(settle, 1, 1140 + kSettleMs + 21));
}

TEST_CASE("A change with the wheel at rest swallows nothing [wheel]") {
    // Escape pressed by somebody who has not touched the wheel at all, and then
    // the wheel: there is no run for the notch to belong to.
    WheelSettle settle;
    settle.focusChanged(1000);
    CHECK_FALSE(swallowed(settle, 1, 1010));

    // The same where the last notch is older than the window.
    WheelSettle stale;
    CHECK_FALSE(swallowed(stale, 1, 1000));
    stale.focusChanged(1500);
    CHECK_FALSE(swallowed(stale, 1, 1510));
}

TEST_CASE("Turning the wheel back is the user and is answered [wheel]") {
    // A tail carries on the way the flick was going. The other way is a hand.
    WheelSettle settle;
    CHECK_FALSE(swallowed(settle, 1, 1000));
    settle.focusChanged(1010);
    CHECK(swallowed(settle, 1, 1020));
    CHECK_FALSE(swallowed(settle, -1, 1040));
    // And from there the wheel is live: the run being swallowed is over.
    CHECK_FALSE(swallowed(settle, -1, 1060));
}

TEST_CASE("A hand that goes on turning gets the new screen back [wheel]") {
    // The cap. A tail dies out well inside it; without it a wheel turned
    // steadily across the change would never reach what it is over.
    WheelSettle settle;
    CHECK_FALSE(swallowed(settle, 1, 1000));
    settle.focusChanged(1000);
    for (Millis at = 1020; at <= 1000 + kCapMs; at += 20) CHECK(swallowed(settle, 1, at));
    CHECK_FALSE(swallowed(settle, 1, 1000 + kCapMs + 20));
    CHECK_FALSE(swallowed(settle, 1, 1000 + kCapMs + 40));
}

TEST_CASE("A box opening over a screen ends the run like a screen does [wheel]") {
    // The reader keeps the screen while the menu stands over it, and the tail
    // would otherwise run the list inside the box to its end. What the guard is
    // told is that something else is in front of the user, not which of the two.
    WheelSettle settle;
    CHECK_FALSE(swallowed(settle, 1, 1000));
    settle.focusChanged(1010);  // the box opened
    CHECK(swallowed(settle, 1, 1020));
    settle.focusChanged(1030);  // and was put away again
    CHECK(swallowed(settle, 1, 1040));
}

TEST_CASE("Anything that is not a notch passes through [wheel]") {
    // Every event goes through the guard, so that it sees the wheel whether or
    // not it is swallowing one; only a notch is ever answered by nothing.
    WheelSettle settle;
    CHECK_FALSE(swallowed(settle, 1, 1000));
    settle.focusChanged(1010);
    CHECK_FALSE(swallowed(settle, 0, 1020));
    // A keystroke in the middle of the tail leaves the tail where it was.
    CHECK(swallowed(settle, 1, 1030));
}

TEST_CASE("wheel_settle_ms 0 leaves the tail where it falls [wheel]") {
    // How the guard is turned off from the config, and what AmberEdit did before
    // it was written: every notch reaches whatever is now in front of the user,
    // a change of screen notwithstanding.
    WheelSettle settle;
    CHECK_FALSE(settle.swallows(1, 1000, kSettleMs, 0));
    settle.focusChanged(1010);
    for (Millis at = 1020; at < 1200; at += 20)
        CHECK_FALSE(settle.swallows(1, at, kSettleMs, 0));

    // And a run swallowed with it on ends the moment it is turned off, rather
    // than going on being swallowed by a guard nobody asked for.
    WheelSettle live;
    CHECK_FALSE(live.swallows(1, 1000, kSettleMs, kCapMs));
    live.focusChanged(1010);
    CHECK(live.swallows(1, 1020, kSettleMs, kCapMs));
    CHECK_FALSE(live.swallows(1, 1040, kSettleMs, 0));
    CHECK_FALSE(live.swallows(1, 1060, kSettleMs, kCapMs));
}
