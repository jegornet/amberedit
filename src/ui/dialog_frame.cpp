#include "ui/dialog_frame.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "ui/text_layout.hpp"

namespace amberedit::ui::dialog {

using namespace term;

/// How far the shadow falls: two columns right and one row down. Two to one
/// because a character cell is about twice as tall as it is wide, so that is
/// what a shadow at 45 degrees comes to on a screen made of them.
constexpr int kShadowRight = 2;
constexpr int kShadowDown = 1;

namespace {

/// A run of rule, and nothing at all where there is no room for one.
/// `horizontalRule()` draws a character even when asked for none — which is
/// right where a rule is the whole row and wrong here, where it would push the
/// far corner a column past the frame above it.
std::string rule(int width) {
    return width > 0 ? horizontalRule(width) : std::string{};
}

/// A label standing in a rule, between the two corners given: what both the top
/// and the bottom of a box are, differing in what they are drawn with and in
/// where the label sits. The title is centred, the way a title is; the keys
/// along the bottom start where reading does.
Element labelledRule(const std::string& left, const std::string& right,
                     const std::string& label, int width, theme::Color tint, bool centred,
                     bool strong = false) {
    const std::string shown = truncateToWidth(label, width);
    const int before = centred ? std::max(0, (width - displayWidth(shown)) / 2) : 0;
    const int after = std::max(0, width - before - displayWidth(shown));
    // Only the label is lit: the rule either side of it is the frame's, and a
    // bold rule would read as a second thing having changed.
    auto middle = text(shown) | color(tint);
    if (strong) middle = std::move(middle) | bold;
    return hbox({text(left + rule(before)) | color(theme::palette.dialogBorder),
                 std::move(middle),
                 text(rule(after) + right) | color(theme::palette.dialogBorder)});
}

}  // namespace

Element titleBar(const std::string& label, int width, theme::Color tint) {
    return labelledRule("╭", "╮", label, width, tint, /*centred=*/true);
}

Element bottomBar(const std::string& hint, const std::string& error, int width) {
    if (!error.empty()) {
        return labelledRule("╰", "╯", " " + error + " ", width, theme::palette.error,
                            /*centred=*/false, /*strong=*/true);
    }
    if (hint.empty()) {
        return text("╰" + horizontalRule(width) + "╯") | color(theme::palette.dialogBorder);
    }
    return labelledRule("╰", "╯", " " + hint + " ", width, theme::palette.dialogHint,
                        /*centred=*/false);
}

Element footerBar(const std::string& label, int width) {
    if (label.empty()) {
        return text("╰" + horizontalRule(width) + "╯") | color(theme::palette.dialogBorder);
    }
    return labelledRule("╰", "╯", " " + label + " ", width, theme::palette.dialogHint,
                        /*centred=*/true);
}

Element surface(Element box) {
    // clear_under is outermost of the three: a decorator draws before its
    // child, so the wipe has to happen before the fill goes down rather than
    // over it. The shadow goes outside all of it — it is the one thing here
    // drawn beside the box rather than on it, and it takes no room, so a box
    // stands where it stood before there were shadows.
    return shadow(std::move(box) | bgcolor(theme::palette.dialogBackground) |
                      color(theme::palette.dialogText) | clear_under,
                  theme::palette.dialogShadow, kShadowRight, kShadowDown);
}

Element framed(Element content, int rows) {
    // A side per row rather than one side stretched: text() paints its top row
    // and leaves the rest of the box it was given alone, so a lone │ beside a
    // button three rows tall would draw a frame with two rows missing out of it.
    const auto side = [rows]() -> Element {
        if (rows <= 1) return text("│") | color(theme::palette.dialogBorder);
        Elements column;
        column.reserve(static_cast<size_t>(rows));
        for (int i = 0; i < rows; ++i) column.push_back(text("│"));
        return vbox(std::move(column)) | color(theme::palette.dialogBorder);
    };
    return hbox({side(), std::move(content), side()});
}

int buttonRows(bool tall) {
    return tall ? 3 : 1;
}

int buttonWidth(const std::string& label, bool tall) {
    // The same number either way, and deliberately: framed, the two columns the
    // frame takes are the two the wider padding takes without it. A box that
    // centres its button by measuring therefore puts it on the same column
    // whichever shape the button is drawn in, and nothing shifts under the user
    // as a window is dragged past the threshold.
    (void)tall;
    return displayWidth(label) + 4;
}

Element button(const std::string& label, bool selected, bool pressed, bool tall) {
    const int inner = displayWidth(label) + 2;
    Element element = tall ? vbox({text("┌" + rule(inner) + "┐"),
                                   text("│ " + label + " │"),
                                   text("└" + rule(inner) + "┘")})
                           : text("  " + label + "  ");

    // Innermost, so that it is the color that lands: a parent paints its whole
    // box and the child paints over it, which is what the fill below relies on.
    if (pressed) element = std::move(element) | color(theme::palette.dialogFlash);
    if (selected) {
        // The same fill as the current row in the lists: one color for whatever
        // Enter would act on, wherever the user is — frame and all, so that a
        // button is picked out from across the box.
        return std::move(element) | bold | color(theme::palette.selectionText) |
               bgcolor(theme::palette.selection);
    }
    return std::move(element) | color(theme::palette.dialogText);
}

Element divider(int width) {
    return text("├" + horizontalRule(width) + "┤") | color(theme::palette.dialogBorder);
}

Element line(const std::string& content, int width, theme::Color tint) {
    return framed(text(padRight(truncateToWidth(content, width), width)) | color(tint));
}

}  // namespace amberedit::ui::dialog
