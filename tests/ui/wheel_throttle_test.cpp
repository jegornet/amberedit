#include <doctest/doctest.h>

#include "ui/wheel_throttle.hpp"

using amberedit::ui::Millis;
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
