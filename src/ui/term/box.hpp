#pragma once

#include <algorithm>

namespace amberedit::ui::term {

/// A rectangle on the screen, in cells, with both edges inclusive.
///
/// Inclusive because that is how the screens hit-test: a click at the far right
/// column of a button is on the button. An empty box is one whose max has fallen
/// below its min, which is what an element given no room comes out as.
struct Box {
    int x_min{0};
    int x_max{0};
    int y_min{0};
    int y_max{0};

    [[nodiscard]] constexpr bool Contain(int x, int y) const {
        return x >= x_min && x <= x_max && y >= y_min && y <= y_max;
    }

    [[nodiscard]] constexpr bool IsEmpty() const {
        return x_min > x_max || y_min > y_max;
    }

    /// A box that contains nothing — what a button the frame did not draw has to
    /// be left as. A default-constructed Box contains the top-left cell of the
    /// screen, which on most screens is a button in its own right.
    [[nodiscard]] static constexpr Box Nowhere() { return Box{1, 0, 1, 0}; }

    [[nodiscard]] static constexpr Box Intersection(Box a, Box b) {
        return Box{
            std::max(a.x_min, b.x_min),
            std::min(a.x_max, b.x_max),
            std::max(a.y_min, b.y_min),
            std::min(a.y_max, b.y_max),
        };
    }

    [[nodiscard]] constexpr bool operator==(const Box& other) const {
        return x_min == other.x_min && x_max == other.x_max && y_min == other.y_min &&
               y_max == other.y_max;
    }
};

}  // namespace amberedit::ui::term
