#include "ui/menu_dialog.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::menu_dialog {

using namespace term;

namespace {

/// The two sides of the frame, which `menu_buttons_width` counts in.
constexpr int kSides = 2;
/// The space in front of the label, inside the frame.
constexpr int kIndent = 1;
/// The narrowest a button can be drawn: the frame with a column of label left
/// inside it. `menu_buttons_width` is held above this, and so is what a window
/// with nothing to spare cuts it back to.
constexpr int kMinWidth = kSides + kIndent + 1;

/// Blank cells between the column of buttons and the edge of the box it stands
/// in — two columns and one row, a terminal cell being about twice as tall as it
/// is wide, so the two read as the same margin all the way round.
constexpr int kMarginX = 2;
constexpr int kMarginY = 1;

/// How wide every button in the menu stands: what `menu_buttons_width` asks for,
/// cut back to what the window has left over once the margins are taken off. The
/// setting decides rather than the labels — see `AppConfig::menuButtonsWidth`.
int buttonWidth(const AppState& state) {
    const int room = state.width - (2 * kMarginX);
    return std::max(kMinWidth, std::min(state.config.menuButtonsWidth, room));
}

/// One button. The label is against the left edge rather than centred: a menu is
/// read down its left-hand side, and words that start in a different column on
/// every row are words the eye has to go looking for.
Element button(const AppState::MenuView::Item& item, int inner, int icon, bool selected,
               bool pressed) {
    const Commands::Info& command = Commands::of(item.command);
    const std::string label =
        labelLine(command.icon, Commands::labelOf(item.command), inner - kIndent, icon);
    const int room = std::max(0, inner - kIndent - displayWidth(label));
    const std::string line = " " + label + std::string(static_cast<size_t>(room), ' ');

    // A disabled button keeps its place and its label: what it says is what the
    // command would do, and leaving it out would move every button under it as
    // the user walks through an area.
    theme::Color tint = theme::palette.dialogHint;
    if (pressed) {
        tint = theme::palette.dialogFlash;
    } else if (selected && item.enabled) {
        tint = theme::palette.selectionText;
    } else if (item.enabled) {
        tint = theme::palette.menuButton;
    }

    auto element = vbox({text("┌" + horizontalRule(inner) + "┐"), text("│" + line + "│"),
                         text("└" + horizontalRule(inner) + "┘")}) |
                   color(tint);
    // The fill the lists give the row Enter would act on, frame and all: what
    // the cursor is on has to be visible from across the box.
    if (selected) element = std::move(element) | bold | bgcolor(theme::palette.selection);
    return element;
}

/// The first command that can actually be run, from `from` and in `step`'s
/// direction, or `from` itself when none can — a menu whose every command is
/// dead is not one anything can move about in.
int enabledFrom(const std::vector<AppState::MenuView::Item>& items, int from, int step) {
    const auto count = static_cast<int>(items.size());
    if (count == 0) return 0;
    for (int i = 0; i < count; ++i) {
        const int at = ((from + (i * step)) % count + count) % count;
        if (items[static_cast<size_t>(at)].enabled) return at;
    }
    return from;
}

/// Moves the cursor, stopping at neither end: a column of buttons is a ring, and
/// a user holding ↓ is asking for the next command. Buttons with nothing to do
/// are stepped over — the cursor is where Enter acts, and it must not come to
/// rest anywhere Enter would do nothing.
void step(AppState::MenuView& view, int delta) {
    const auto count = static_cast<int>(view.items.size());
    if (count == 0) return;
    const int next = ((view.cursor + delta) % count + count) % count;
    view.cursor = enabledFrom(view.items, next, delta >= 0 ? 1 : -1);
}

}  // namespace

int iconWidth(const std::vector<AppState::MenuView::Item>& items) {
    int width = 0;
    for (const AppState::MenuView::Item& item : items) {
        width = std::max(width, displayWidth(Commands::of(item.command).icon));
    }
    return width;
}

std::string labelLine(std::string_view icon, std::string_view word, int columns,
                      int iconColumns) {
    if (columns <= 0) return {};
    if (icon.empty() && iconColumns <= 0) return truncateToWidth(word, columns);

    // The glyph is measured with what the renderer draws it by rather than
    // counted: `⚠︎` is two code points in one column, `𝒊` four bytes in one, and
    // an emoji one glyph in two — and the platform's wcwidth() is what settles
    // any of them. Counting instead would hand the word a column the glyph is
    // about to take, and the button would be drawn a column wider than the
    // column it stands in.
    const std::string column = padRight(icon, iconColumns);
    const int room = columns - displayWidth(column) - 1;  // the blank between
    if (room <= 0) return truncateToWidth(icon, columns);
    return column + " " + truncateToWidth(word, room);
}

void open(AppState& state, std::vector<AppState::MenuView::Item> items) {
    if (items.empty()) return;

    AppState::MenuView view;
    view.items = std::move(items);
    view.cursor = enabledFrom(view.items, 0, 1);
    state.menuView = std::move(view);
}

Command current(const AppState& state) {
    const AppState::MenuView& view = *state.menuView;
    const auto at = static_cast<size_t>(
        std::clamp(view.cursor, 0, static_cast<int>(view.items.size()) - 1));
    return view.items[at].command;
}

Element render(AppState& state, Element background) {
    AppState::MenuView& view = *state.menuView;
    const int width = buttonWidth(state);
    const int inner = width - kSides;
    // One column for the glyphs, as wide as the widest of them: the words below
    // one another all start in the same place, which is what a column is read
    // down.
    const int icon = iconWidth(view.items);

    // The margin at either hand of a button, and the blank rows over and under
    // the column: what marks the buttons off from the edge of the box, so that
    // the frames are not flush against the screen the menu stands over.
    const std::string side(static_cast<size_t>(kMarginX), ' ');
    const std::string blank(static_cast<size_t>(width + (2 * kMarginX)), ' ');

    Elements rows;
    rows.reserve(view.items.size() + static_cast<size_t>(2 * kMarginY));
    for (int i = 0; i < kMarginY; ++i) rows.push_back(text(blank));
    // Button under button, with nothing between them: the frames meet, and the
    // column reads as one list rather than as a handful of boxes.
    for (size_t i = 0; i < view.items.size(); ++i) {
        const bool pressed =
            state.isPressed(AppState::Pressed::Menu, static_cast<uint32_t>(i));
        rows.push_back(hbox({text(side),
                             button(view.items[i], inner, icon,
                                    static_cast<int>(i) == view.cursor, pressed) |
                                 reflect(view.items[i].box),
                             text(side)}));
    }
    for (int i = 0; i < kMarginY; ++i) rows.push_back(text(blank));

    // dialog::surface() wipes the screen behind the menu and lays the dialog's
    // own fill down in its place, so what it stands over neither shows through
    // its margins nor colors them.
    return dbox({std::move(background), dialog::surface(vbox(std::move(rows))) | center});
}

Outcome handleEvent(AppState& state, const Event& event) {
    AppState::MenuView& view = *state.menuView;

    if (const auto click = leftClick(event)) {
        for (size_t i = 0; i < view.items.size(); ++i) {
            if (!view.items[i].box.Contain(click->x, click->y)) continue;
            // A button with nothing to do swallows the click: it is still a
            // click on the menu, and letting it through to the screen underneath
            // would be worse than doing nothing.
            if (!view.items[i].enabled) return Outcome::Ignored;
            // Moved onto first and shown pressed after, so that the button the
            // pointer landed on is the current one for the length of the
            // animation before whatever it opens takes the screen.
            view.cursor = static_cast<int>(i);
            state.showClick(AppState::Pressed::Menu, static_cast<uint32_t>(i));
            return Outcome::Picked;
        }
        // Anywhere else on the screen. A menu one has thought better of is
        // dismissed by pointing away from it — that is what a click outside one
        // means everywhere else, and the screen underneath is not acted on.
        state.menuView.reset();
        return Outcome::Dismissed;
    }

    if (const int wheel = wheelDelta(event); wheel != 0) {
        step(view, wheel);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowDown || event == Event::Tab) {
        step(view, 1);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowUp || event == Event::TabReverse) {
        step(view, -1);
        return Outcome::Ignored;
    }
    if (event == Event::Home) {
        view.cursor = enabledFrom(view.items, 0, 1);
        return Outcome::Ignored;
    }
    if (event == Event::End) {
        view.cursor =
            enabledFrom(view.items, static_cast<int>(view.items.size()) - 1, -1);
        return Outcome::Ignored;
    }
    if (event == Event::Return || event == Event::Character(' ')) {
        // The cursor never rests on a dead command, so this can only be a menu
        // in which nothing at all can be run.
        const auto at = static_cast<size_t>(view.cursor);
        return view.items[at].enabled ? Outcome::Picked : Outcome::Ignored;
    }
    if (event == Event::Escape || event == Event::Backspace) {
        state.menuView.reset();
        return Outcome::Dismissed;
    }
    return Outcome::Ignored;
}

}  // namespace amberedit::ui::menu_dialog
