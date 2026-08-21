#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ui/term/box.hpp"
#include "ui/term/color.hpp"

namespace amberedit::ui::term {

/// The typeface bits a cell can carry. Held as a mask rather than four bools so
/// that a cell stays small — there is one per column of the screen, redrawn
/// every frame.
enum Attr : uint8_t {
    kBold = 1u << 0u,
    kItalic = 1u << 1u,
    kInverted = 1u << 2u,
    kUnderlined = 1u << 3u,
};

/// One character cell.
///
/// `glyph` holds a whole grapheme — a base character with any combining marks
/// already attached — rather than a single code point, so that an accented
/// letter occupies the one column it is drawn in. The empty string is not a
/// blank: it marks the second column of a double-width glyph, whose first
/// column carries the whole thing. A blank is a space.
struct Cell {
    std::string glyph{" "};
    Color fg{};
    Color bg{};
    uint8_t attrs{0};
};

/// The frame being built. Rendering paints into this, and only once it is
/// finished does anything reach the terminal — which is what makes the modal
/// dialogs possible, since they are drawn over a background already rendered.
class Screen {
public:
    Screen(int width, int height);

    /// The cell at (x, y). Out-of-range coordinates give back a scratch cell
    /// that is thrown away, so that a render pass which computed a position
    /// badly draws nothing rather than corrupting memory. Elements are clipped
    /// by their box long before this, so reaching it means a bug — but a bug
    /// that loses a character is much easier to live with than one that does not.
    [[nodiscard]] Cell& at(int x, int y);
    [[nodiscard]] const Cell& at(int x, int y) const;

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }

    /// Puts every cell back to a blank in the terminal's own colors.
    void clear();

    /// Reallocates for a new terminal size, clearing in the process.
    void resize(int width, int height);

    /// The region drawing is confined to — the whole screen, in practice. Kept
    /// so that `reflect` can clip a hit-box to what is actually visible: a
    /// button half off the edge must not be clickable where it is not drawn.
    Box stencil{};

private:
    int width_{0};
    int height_{0};
    std::vector<Cell> cells_;
    Cell scratch_{};
};

}  // namespace amberedit::ui::term
