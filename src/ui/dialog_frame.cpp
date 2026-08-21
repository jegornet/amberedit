#include "ui/dialog_frame.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "ui/text_layout.hpp"

namespace amberedit::ui::dialog {

using namespace term;

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
    return hbox({text(left + rule(before)) | color(theme::palette.separator),
                 std::move(middle),
                 text(rule(after) + right) | color(theme::palette.separator)});
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
        return text("╰" + horizontalRule(width) + "╯") | color(theme::palette.separator);
    }
    return labelledRule("╰", "╯", " " + hint + " ", width, theme::palette.footer,
                        /*centred=*/false);
}

Element footerBar(const std::string& label, int width) {
    if (label.empty()) {
        return text("╰" + horizontalRule(width) + "╯") | color(theme::palette.separator);
    }
    return labelledRule("╰", "╯", " " + label + " ", width, theme::palette.footer,
                        /*centred=*/true);
}

Element framed(Element content) {
    const auto side = [] { return text("│") | color(theme::palette.separator); };
    return hbox({side(), std::move(content), side()});
}

Element divider(int width) {
    return text("├" + horizontalRule(width) + "┤") | color(theme::palette.separator);
}

Element line(const std::string& content, int width, theme::Color tint) {
    return framed(text(padRight(truncateToWidth(content, width), width)) | color(tint));
}

}  // namespace amberedit::ui::dialog
