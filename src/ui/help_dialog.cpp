#include "ui/help_dialog.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "i18n/i18n.hpp"
#include "ui/command_live.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/keys.hpp"
#include "ui/scrollbar.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::help_dialog {

using namespace term;

namespace {

/// How wide the box stands inside its frame, and the narrowest it is squeezed to
/// in a window with less than that.
///
/// A width, not a measurement of what is in it — the same rule the nodelist's
/// box is built on. A box measured against the longest sentence in it would be
/// one width in the reader and another in the editor, and F1 would move the text
/// under the eye from screen to screen.
constexpr int kInnerWidth = 72;
constexpr int kMinInner = 30;
/// The frame itself, a column on each side and a row above and below.
constexpr int kFrame = 2;
constexpr int kChromeRows = 2;
/// The rows it keeps clear of the top and bottom of the window.
constexpr int kWindowMargin = 2;

/// How wide the keys stand at most. The column itself is the widest of them and
/// no wider — this is the cap on that. `Ctrl-W, Alt-Backspace` is the longest
/// the standard layout writes and it fits whole; a layout that wrote a longer
/// one has it cut rather than taking the column the sentences need.
constexpr int kMaxKeyColumn = 22;
/// The space between the keys and the sentence beside them.
constexpr int kGap = 2;

/// What the box is called, which is the screen it was opened on: the box says
/// what these keys are the keys *of*, and the block below is that screen's.
const char* titleOf(app::ScreenId screen) {
    switch (screen) {
        case app::ScreenId::AreaList: return _(" Area list keys ");
        case app::ScreenId::MessageList: return _(" Message list keys ");
        case app::ScreenId::MessageRead: return _(" Reader keys ");
        case app::ScreenId::Compose: break;
    }
    return _(" Editor keys ");
}

/// Which of the command table's screens answers for the one that is up.
CommandScreen commandScreenOf(app::ScreenId screen) {
    switch (screen) {
        case app::ScreenId::AreaList: return CommandScreen::AreaList;
        case app::ScreenId::MessageList: return CommandScreen::MessageList;
        case app::ScreenId::MessageRead: return CommandScreen::Reader;
        case app::ScreenId::Compose: break;
    }
    return CommandScreen::Compose;
}

/// Every key the layout runs the command on, written as a `keys` file writes
/// them and in the order it gave them: `q, F4`.
///
/// All of them rather than the shortest, which is what the hint bar shows: a row
/// of the screen has room for one key and this box has room for the answer. A
/// hand that has learned `F4` and reads only `q` would take the box for a
/// layout it is not.
std::string keysOf(const AppState& state, Command command) {
    std::string keys;
    for (const Event& key : state.keys.keysOf(command)) {
        if (!keys.empty()) keys += ", ";
        keys += spellingOf(key);
    }
    return keys;
}

/// The rows for the screen that is up: its own commands, and then the ones
/// answered before every screen under a heading of their own.
///
/// A command the layout leaves unbound is left out, and so is one the screen
/// cannot act on as it stands — `commandLive()`, which is the same question the
/// hint bar asks of the same keyboard.
void build(AppState& state, AppState::HelpView& view) {
    const CommandScreen screen = commandScreenOf(state.navigator.current());
    const auto add = [&state, &view](Command command) {
        if (!commandLive(state, command)) return;
        std::string keys = keysOf(state, command);
        if (keys.empty()) return;
        view.lines.push_back({std::move(keys), state.config.helpTextOf(command), false});
    };

    for (const Commands::Info& info : Commands::all()) {
        if (info.screen == screen) add(info.command);
    }

    const size_t own = view.lines.size();
    for (const Commands::Info& info : Commands::all()) {
        if (info.screen == CommandScreen::Anywhere) add(info.command);
    }
    // The heading over the second block, and a blank row to mark it off from the
    // first — put in afterwards, since neither is worth a row where nothing was
    // added under it.
    if (view.lines.size() > own) {
        view.lines.insert(view.lines.begin() + static_cast<long>(own),
                          AppState::HelpView::Line{{}, _("Everywhere"), true});
        if (own != 0) {
            view.lines.insert(view.lines.begin() + static_cast<long>(own),
                              AppState::HelpView::Line{});
        }
    }

    // A layout that has bound nothing this screen answers — `keys_mode clear`
    // and a file that names none of it. The box opens either way, and saying so
    // is the whole of what there is to say.
    if (view.lines.empty()) {
        view.lines.push_back({{}, _("no key on this screen runs anything"), false});
    }

    for (const AppState::HelpView::Line& line : view.lines) {
        view.keyColumn =
            std::max(view.keyColumn, std::min(kMaxKeyColumn, displayWidth(line.keys)));
    }
}

/// Settles how big the box is, once — and again only where the window itself has
/// changed size, exactly as the nodelist's box does it.
void fitBox(const AppState& state, AppState::HelpView& view) {
    if (view.layoutWidth == state.width && view.layoutHeight == state.height) return;
    view.layoutWidth = state.width;
    view.layoutHeight = state.height;
    view.inner = std::min(kInnerWidth, std::max(kMinInner, state.width - kFrame));
    view.rows = std::max(1, std::min(static_cast<int>(view.lines.size()),
                                     state.height - kChromeRows - kWindowMargin));
}

/// One row as it is drawn: the keys in their column, the sentence beside them,
/// and the whole of it cut to the width the box has.
///
/// A heading carries no keys and starts where they do, the block under it being
/// what it is a heading over.
std::string rowText(const AppState::HelpView& view, const AppState::HelpView::Line& line,
                    int width) {
    if (line.heading) return truncateToWidth(" " + line.text, width);
    const std::string keys =
        padRight(truncateToWidth(line.keys, view.keyColumn), view.keyColumn);
    return truncateToWidth(" " + keys + std::string(kGap, ' ') + line.text, width);
}

}  // namespace

void open(AppState& state) {
    AppState::HelpView view;
    build(state, view);
    state.helpView = std::move(view);
}

Element render(AppState& state, Element background) {
    AppState::HelpView& view = *state.helpView;
    fitBox(state, view);

    const int inner = view.inner;
    const auto total = static_cast<int>(view.lines.size());
    view.scroll = std::clamp(view.scroll, 0, std::max(0, total - view.rows));

    // The bar the reader draws beside a message too long for the window, in the
    // rightmost column of the box and only where the rows do not all fit — the
    // nodelist's box shows it the same way and for the same reason.
    const bool scrollbarShown = total > view.rows;
    const int listWidth = scrollbarShown ? std::max(1, inner - 1) : inner;
    const scrollbar::Thumb thumb = scrollbar::thumbOf(view.rows, total, view.scroll);

    Elements lines{dialog::titleBar(titleOf(state.navigator.current()), inner)};
    for (int i = 0; i < view.rows; ++i) {
        const int at = view.scroll + i;
        const bool heading = at < total && view.lines[static_cast<size_t>(at)].heading;
        const std::string content =
            at < total ? rowText(view, view.lines[static_cast<size_t>(at)], listWidth)
                       : std::string{};

        Element row =
            text(padRight(content, listWidth)) |
            color(heading ? theme::palette.dialogLabel : theme::palette.dialogText);
        if (heading) row = std::move(row) | bold;
        lines.push_back(scrollbarShown ? dialog::framed(hbox(
                                             {std::move(row), scrollbar::cell(i, thumb)}))
                                       : dialog::framed(std::move(row)));
    }
    lines.push_back(dialog::bottomBar(_("Esc closes"), {}, inner));

    return dbox(
        {std::move(background), dialog::surface(vbox(std::move(lines))) | center});
}

void handleEvent(AppState& state, const Event& event) {
    AppState::HelpView& view = *state.helpView;
    const auto total = static_cast<int>(view.lines.size());
    const int last = std::max(0, total - view.rows);
    const auto scrollBy = [&view, last](int delta) {
        view.scroll = std::clamp(view.scroll + delta, 0, last);
    };

    // A click anywhere puts the box away. There is nothing in it to point at —
    // it shows and does not ask — so a click on it can only mean "done".
    if (leftClick(event)) {
        state.helpView.reset();
        return;
    }
    if (const int wheel = wheelDelta(event); wheel != 0) {
        scrollBy(wheel);
        return;
    }
    if (event == Event::ArrowDown) {
        scrollBy(1);
        return;
    }
    if (event == Event::ArrowUp) {
        scrollBy(-1);
        return;
    }
    if (event == Event::PageDown || event == Event::Character(' ')) {
        scrollBy(view.rows);
        return;
    }
    if (event == Event::PageUp) {
        scrollBy(-view.rows);
        return;
    }
    if (event == Event::Home) {
        view.scroll = 0;
        return;
    }
    if (event == Event::End) {
        view.scroll = last;
        return;
    }
    // The same keys every box that only shows closes on, and whatever key opened
    // this to close it again — so that whichever the hand reaches for puts it
    // away.
    if (event == Event::Escape || event == Event::Backspace || event == Event::Return ||
        state.keys.is(event, Command::AppHelp)) {
        state.helpView.reset();
    }
}

}  // namespace amberedit::ui::help_dialog
