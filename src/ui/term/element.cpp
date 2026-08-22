#include "ui/term/element.hpp"

#include <algorithm>
#include <utility>

#include "ui/term/utf8.hpp"

namespace amberedit::ui::term {
namespace {

/// One child's share of a container, while it is being worked out.
struct Share {
    int min_size{0};
    int flex_grow{0};
    int flex_shrink{0};
    int size{0};
};

/// Shares `target` out among the children, following FTXUI's rules so that the
/// screens lay out exactly as they did before the move off it.
///
/// Three cases. With room to spare, the surplus goes to whoever will grow, in
/// proportion. With too little and enough give among those that will shrink, the
/// shortfall is taken from them in proportion to what they asked for. With too
/// little even for that, everything that will shrink goes to nothing and the
/// remaining shortfall is taken from those that would not.
void computeShares(std::vector<Share>& shares, int target) {
    int size = 0;
    int growSum = 0;
    int shrinkSum = 0;
    int shrinkSize = 0;
    for (const auto& share : shares) {
        growSum += share.flex_grow;
        shrinkSum += share.min_size * share.flex_shrink;
        if (share.flex_shrink != 0) shrinkSize += share.min_size;
        size += share.min_size;
    }

    int extra = target - size;
    if (extra >= 0) {
        for (auto& share : shares) {
            const int added = extra * share.flex_grow / std::max(growSum, 1);
            extra -= added;
            growSum -= share.flex_grow;
            share.size = share.min_size + added;
        }
        return;
    }

    if (shrinkSize + extra >= 0) {
        for (auto& share : shares) {
            const int added =
                extra * share.min_size * share.flex_shrink / std::max(shrinkSum, 1);
            extra -= added;
            shrinkSum -= share.flex_shrink * share.min_size;
            share.size = share.min_size + added;
        }
        return;
    }

    extra += shrinkSize;
    int remaining = size - shrinkSize;
    for (auto& share : shares) {
        if (share.flex_shrink != 0) {
            share.size = 0;
            continue;
        }
        const int added = extra * share.min_size / std::max(1, remaining);
        extra -= added;
        remaining -= share.min_size;
        share.size = share.min_size + added;
    }
}

class Text : public Node {
public:
    explicit Text(std::string content) : content_(std::move(content)) {}

    void ComputeRequirement() override {
        requirement_.min_x = stringWidth(content_);
        requirement_.min_y = 1;
    }

    void Render(Screen& screen) override {
        // One row only, and only if there is one to draw on.
        if (box_.y_min > box_.y_max) return;

        int x = box_.x_min;
        const int y = box_.y_min;
        for (const auto& glyph : toGlyphs(content_)) {
            if (x > box_.x_max) break;
            // The empty entry standing for the second column of a wide glyph is
            // written as it is: whoever draws the frame out steps over it.
            screen.at(x, y).glyph = glyph;
            ++x;
        }
    }

private:
    std::string content_;
};

class HBox : public Node {
public:
    using Node::Node;

    void ComputeRequirement() override {
        requirement_ = Requirement{};
        for (auto& child : children_) {
            child->ComputeRequirement();
            requirement_.min_x += child->requirement().min_x;
            requirement_.min_y =
                std::max(requirement_.min_y, child->requirement().min_y);
        }
    }

    void SetBox(Box box) override {
        Node::SetBox(box);

        std::vector<Share> shares(children_.size());
        for (size_t i = 0; i < children_.size(); ++i) {
            const auto& requirement = children_[i]->requirement();
            shares[i].min_size = requirement.min_x;
            shares[i].flex_grow = requirement.flex_grow_x;
            shares[i].flex_shrink = requirement.flex_shrink_x;
        }
        computeShares(shares, box.x_max - box.x_min + 1);

        int x = box.x_min;
        for (size_t i = 0; i < children_.size(); ++i) {
            Box child = box;
            child.x_min = x;
            child.x_max = x + shares[i].size - 1;
            children_[i]->SetBox(child);
            x = child.x_max + 1;
        }
    }
};

class VBox : public Node {
public:
    using Node::Node;

    void ComputeRequirement() override {
        requirement_ = Requirement{};
        for (auto& child : children_) {
            child->ComputeRequirement();
            requirement_.min_y += child->requirement().min_y;
            requirement_.min_x =
                std::max(requirement_.min_x, child->requirement().min_x);
        }
    }

    void SetBox(Box box) override {
        Node::SetBox(box);

        std::vector<Share> shares(children_.size());
        for (size_t i = 0; i < children_.size(); ++i) {
            const auto& requirement = children_[i]->requirement();
            shares[i].min_size = requirement.min_y;
            shares[i].flex_grow = requirement.flex_grow_y;
            shares[i].flex_shrink = requirement.flex_shrink_y;
        }
        computeShares(shares, box.y_max - box.y_min + 1);

        int y = box.y_min;
        for (size_t i = 0; i < children_.size(); ++i) {
            Box child = box;
            child.y_min = y;
            child.y_max = y + shares[i].size - 1;
            children_[i]->SetBox(child);
            y = child.y_max + 1;
        }
    }
};

class DBox : public Node {
public:
    using Node::Node;

    void ComputeRequirement() override {
        requirement_ = Requirement{};
        for (auto& child : children_) {
            child->ComputeRequirement();
            requirement_.min_x =
                std::max(requirement_.min_x, child->requirement().min_x);
            requirement_.min_y =
                std::max(requirement_.min_y, child->requirement().min_y);
        }
    }

    void SetBox(Box box) override {
        Node::SetBox(box);
        for (auto& child : children_) child->SetBox(box);
    }
};

/// A node that wraps exactly one child and neither takes room from it nor gives
/// it any: the base of every decorator.
class Decorated : public Node {
public:
    explicit Decorated(Element child) : Node(Elements{std::move(child)}) {}

    void ComputeRequirement() override {
        Node::ComputeRequirement();
        requirement_ = children_[0]->requirement();
    }

    void SetBox(Box box) override {
        Node::SetBox(box);
        children_[0]->SetBox(box);
    }
};

/// Marks the child — or nothing at all, which is what `filler()` is — as willing
/// to give and take room.
class Flex : public Node {
public:
    Flex() = default;
    explicit Flex(Element child) : Node(Elements{std::move(child)}) {}

    void ComputeRequirement() override {
        requirement_ = Requirement{};
        if (!children_.empty()) {
            children_[0]->ComputeRequirement();
            requirement_ = children_[0]->requirement();
        }
        requirement_.flex_grow_x = 1;
        requirement_.flex_grow_y = 1;
        requirement_.flex_shrink_x = 1;
        requirement_.flex_shrink_y = 1;
    }

    void SetBox(Box box) override {
        Node::SetBox(box);
        if (!children_.empty()) children_[0]->SetBox(box);
    }
};

class Border : public Node {
public:
    explicit Border(Element child) : Node(Elements{std::move(child)}) {}

    void ComputeRequirement() override {
        Node::ComputeRequirement();
        requirement_ = children_[0]->requirement();
        requirement_.min_x += 2;
        requirement_.min_y += 2;
    }

    void SetBox(Box box) override {
        Node::SetBox(box);
        Box inner = box;
        inner.x_min++;
        inner.x_max--;
        inner.y_min++;
        inner.y_max--;
        children_[0]->SetBox(inner);
    }

    void Render(Screen& screen) override {
        // Contents first, then the frame over them: a child that overflowed
        // must not be what closes the box.
        Node::Render(screen);
        if (box_.x_min >= box_.x_max || box_.y_min >= box_.y_max) return;

        screen.at(box_.x_min, box_.y_min).glyph = "┌";
        screen.at(box_.x_max, box_.y_min).glyph = "┐";
        screen.at(box_.x_min, box_.y_max).glyph = "└";
        screen.at(box_.x_max, box_.y_max).glyph = "┘";
        for (int x = box_.x_min + 1; x < box_.x_max; ++x) {
            screen.at(x, box_.y_min).glyph = "─";
            screen.at(x, box_.y_max).glyph = "─";
        }
        for (int y = box_.y_min + 1; y < box_.y_max; ++y) {
            screen.at(box_.x_min, y).glyph = "│";
            screen.at(box_.x_max, y).glyph = "│";
        }
    }
};

class ClearUnder : public Decorated {
public:
    using Decorated::Decorated;

    void Render(Screen& screen) override {
        for (int y = box_.y_min; y <= box_.y_max; ++y) {
            for (int x = box_.x_min; x <= box_.x_max; ++x) screen.at(x, y) = Cell{};
        }
        Node::Render(screen);
    }
};

/// A shadow falling to the right of the element and below it, the way a box laid
/// over a screen would cast one.
///
/// It asks for no room of its own and leaves the element the box it was given:
/// a shadow is cast on what is already there, so every dialog keeps the size and
/// the place it had before there were shadows. That is also why this is the one
/// node that paints outside its own box — `Screen::at()` drops what falls off
/// the edge, so a box standing against the right-hand side simply casts less.
///
/// The strips go down before the element: they never overlap it, but the fill
/// under a box is laid by the same call, and the element has to be able to paint
/// over what it covers.
class Shadow : public Decorated {
public:
    Shadow(Element child, Color color, int dx, int dy)
        : Decorated(std::move(child)), color_(color), dx_(dx), dy_(dy) {}

    void Render(Screen& screen) override {
        // Down the right-hand side, starting `dy_` rows lower, and along the
        // bottom, starting `dx_` columns further right: a shadow is the box
        // moved, not the box grown.
        for (int y = box_.y_min + dy_; y <= box_.y_max + dy_; ++y) {
            for (int x = box_.x_max + 1; x <= box_.x_max + dx_; ++x) fall(screen, x, y);
        }
        for (int y = box_.y_max + 1; y <= box_.y_max + dy_; ++y) {
            for (int x = box_.x_min + dx_; x <= box_.x_max; ++x) fall(screen, x, y);
        }
        Node::Render(screen);
    }

private:
    void fall(Screen& screen, int x, int y) const {
        if (x < 0 || y < 0 || x >= screen.width() || y >= screen.height()) return;
        screen.at(x, y) = Cell{" ", color_, color_, 0};
    }

    Color color_;
    int dx_;
    int dy_;
};

/// Paints one attribute over the whole box and then draws the child on top.
/// Drawing in that order is what makes the innermost decorator the one that
/// shows: a colored run inside a colored line is painted second.
class Styled : public Decorated {
public:
    Styled(Element child, uint8_t attrs) : Decorated(std::move(child)), attrs_(attrs) {}

    void Render(Screen& screen) override {
        for (int y = box_.y_min; y <= box_.y_max; ++y) {
            for (int x = box_.x_min; x <= box_.x_max; ++x) {
                screen.at(x, y).attrs |= attrs_;
            }
        }
        Node::Render(screen);
    }

private:
    uint8_t attrs_;
};

class Painted : public Decorated {
public:
    Painted(Element child, Color color, bool background)
        : Decorated(std::move(child)), color_(color), background_(background) {}

    void Render(Screen& screen) override {
        for (int y = box_.y_min; y <= box_.y_max; ++y) {
            for (int x = box_.x_min; x <= box_.x_max; ++x) {
                Cell& cell = screen.at(x, y);
                if (background_) {
                    cell.bg = color_;
                } else {
                    cell.fg = color_;
                }
            }
        }
        Node::Render(screen);
    }

private:
    Color color_;
    bool background_;
};

class Reflect : public Decorated {
public:
    Reflect(Element child, Box& target) : Decorated(std::move(child)), target_(target) {}

    void SetBox(Box box) override {
        target_ = box;
        Decorated::SetBox(box);
    }

    void Render(Screen& screen) override {
        // Clipped to what is actually on the screen, so that a row scrolled half
        // out of view is not clickable where it is not drawn.
        target_ = Box::Intersection(screen.stencil, target_);
        Node::Render(screen);
    }

private:
    Box& target_;
};

}  // namespace

Node::Node(Elements children) : children_(std::move(children)) {}
Node::~Node() = default;

void Node::ComputeRequirement() {
    if (children_.empty()) return;
    for (auto& child : children_) child->ComputeRequirement();
    requirement_ = children_[0]->requirement();
}

void Node::SetBox(Box box) { box_ = box; }

void Node::Render(Screen& screen) {
    for (auto& child : children_) child->Render(screen);
}

Element text(std::string content) {
    return std::make_shared<Text>(std::move(content));
}
Element hbox(Elements children) { return std::make_shared<HBox>(std::move(children)); }
Element vbox(Elements children) { return std::make_shared<VBox>(std::move(children)); }
Element dbox(Elements children) { return std::make_shared<DBox>(std::move(children)); }

Element filler() { return std::make_shared<Flex>(); }
Element flex(Element child) { return std::make_shared<Flex>(std::move(child)); }
Element border(Element child) { return std::make_shared<Border>(std::move(child)); }

Element hcenter(Element child) {
    return hbox({filler(), std::move(child), filler()});
}
Element vcenter(Element child) {
    return vbox({filler(), std::move(child), filler()});
}
Element center(Element child) { return hcenter(vcenter(std::move(child))); }

Element clear_under(Element child) {
    return std::make_shared<ClearUnder>(std::move(child));
}

Element shadow(Element child, Color color, int dx, int dy) {
    return std::make_shared<Shadow>(std::move(child), color, dx, dy);
}

Element bold(Element child) { return std::make_shared<Styled>(std::move(child), kBold); }
Element italic(Element child) {
    return std::make_shared<Styled>(std::move(child), kItalic);
}
Element inverted(Element child) {
    return std::make_shared<Styled>(std::move(child), kInverted);
}
Element underlined(Element child) {
    return std::make_shared<Styled>(std::move(child), kUnderlined);
}

Element color(Color color, Element child) {
    return std::make_shared<Painted>(std::move(child), color, false);
}
Element bgcolor(Color color, Element child) {
    return std::make_shared<Painted>(std::move(child), color, true);
}
Decorator color(Color color) {
    return [color](Element child) { return term::color(color, std::move(child)); };
}
Decorator bgcolor(Color color) {
    return [color](Element child) { return term::bgcolor(color, std::move(child)); };
}

Decorator reflect(Box& box) {
    return [&box](Element child) -> Element {
        return std::make_shared<Reflect>(std::move(child), box);
    };
}

Element operator|(Element element, Decorator decorator) {
    return decorator(std::move(element));
}

Element& operator|=(Element& element, Decorator decorator) {
    element = decorator(std::move(element));
    return element;
}

void render(Screen& screen, const Element& document) {
    screen.clear();
    document->ComputeRequirement();
    document->SetBox(Box{0, screen.width() - 1, 0, screen.height() - 1});
    document->Render(screen);
}

}  // namespace amberedit::ui::term
