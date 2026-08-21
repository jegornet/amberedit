#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/term/box.hpp"
#include "ui/term/color.hpp"
#include "ui/term/screen.hpp"

/// The small tree of boxes the interface is described with.
///
/// This is a deliberate stand-in for the part of FTXUI's dom that AmberEdit used —
/// the same names, the same composition with `|`, and the same layout rules —
/// so that the screens themselves did not have to be rewritten to move off it.
/// What is here is only what the screens actually ask for: there are no widgets,
/// no focus, no flexbox beyond a single growing child, because AmberEdit computes
/// its own columns and does its own scrolling and never wanted them.
///
/// Drawing happens in three passes, which is where the layout rules live:
///   1. `ComputeRequirement()` walks up from the leaves working out how much
///      room each element needs;
///   2. `SetBox()` walks back down handing out the room there actually is;
///   3. `Render()` paints, parents before children — which is what makes the
///      innermost color win, since it is applied last.
namespace amberedit::ui::term {

/// How much room an element needs, and what it will do with more or less.
struct Requirement {
    int min_x{0};
    int min_y{0};
    int flex_grow_x{0};
    int flex_grow_y{0};
    int flex_shrink_x{0};
    int flex_shrink_y{0};
};

class Node;
using Element = std::shared_ptr<Node>;
using Elements = std::vector<Element>;
using Decorator = std::function<Element(Element)>;

class Node {
public:
    Node() = default;
    explicit Node(Elements children);
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&) = delete;
    Node& operator=(Node&&) = delete;
    virtual ~Node();

    /// Works out `requirement_` from the children's. The default asks the
    /// children and takes the first one's answer, which is what every decorator
    /// wants.
    virtual void ComputeRequirement();

    /// Accepts the room this element has been given, and shares it out.
    virtual void SetBox(Box box);

    /// Paints. The default just passes the screen to the children.
    virtual void Render(Screen& screen);

    [[nodiscard]] const Requirement& requirement() const { return requirement_; }

protected:
    Elements children_;
    Requirement requirement_{};
    Box box_{};
};

/// A run of text on one row. The single leaf: everything else arranges these.
[[nodiscard]] Element text(std::string content);

/// Children side by side, left to right.
[[nodiscard]] Element hbox(Elements children);
/// Children stacked, top to bottom.
[[nodiscard]] Element vbox(Elements children);
/// Children on top of one another, each given the whole box and drawn in turn —
/// which is how a dialog is put over the screen it interrupts.
[[nodiscard]] Element dbox(Elements children);

/// Empty, and takes whatever room is going. What centring is built from.
[[nodiscard]] Element filler();

/// Lets an element grow into the room its container has left over, and shrink
/// when there is not enough.
[[nodiscard]] Element flex(Element child);

/// A light box drawn around the element, costing one row and column on each
/// side. The border takes its color from whatever encloses it.
[[nodiscard]] Element border(Element child);

[[nodiscard]] Element hcenter(Element child);
[[nodiscard]] Element vcenter(Element child);
[[nodiscard]] Element center(Element child);

/// Blanks the cells under the element before it is drawn — the colors and the
/// attributes both, back to the terminal's own. Used with `dbox` so that what a
/// dialog covers does not show through it, and never on its own: the cells it
/// leaves are in a color no theme chose, so `dialog::surface()` puts the box's
/// own fill down over them in the same breath.
[[nodiscard]] Element clear_under(Element child);

[[nodiscard]] Element bold(Element child);
[[nodiscard]] Element italic(Element child);
[[nodiscard]] Element inverted(Element child);
[[nodiscard]] Element underlined(Element child);

[[nodiscard]] Element color(Color color, Element child);
[[nodiscard]] Element bgcolor(Color color, Element child);
[[nodiscard]] Decorator color(Color color);
[[nodiscard]] Decorator bgcolor(Color color);

/// Writes back where the element was finally drawn, so that a click can be
/// tested against what is on the screen rather than against a position worked
/// out a second time and liable to drift from it.
[[nodiscard]] Decorator reflect(Box& box);

Element operator|(Element element, Decorator decorator);
Element& operator|=(Element& element, Decorator decorator);

/// Lays the document out to fill the screen and draws it, clearing first.
void render(Screen& screen, const Element& document);

}  // namespace amberedit::ui::term
