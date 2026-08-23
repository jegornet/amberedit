#include "ui/hint_bar.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/text_util.hpp"
#include "ui/event_util.hpp"
#include "ui/keys.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::hint_bar {
namespace {

using namespace term;

/// What the screen that is up offers, in the order it is offered in: its own
/// hint list, from the config.
///
/// One list per screen and each of them the user's, so a screen has exactly the
/// hints that were asked for — see `AppConfig::readerHints` and the three beside
/// it for what they hold when nothing was.
const std::vector<Command>& commandsOf(const AppState& state) {
    switch (state.navigator.current()) {
        case app::ScreenId::AreaList: return state.config.arealistHints;
        case app::ScreenId::MessageList: return state.config.msglistHints;
        case app::ScreenId::MessageRead: return state.config.readerHints;
        case app::ScreenId::Compose: return state.config.composeHints;
    }
    return state.config.msglistHints;  // unreachable; a ScreenId is one of the four
}

/// One hint: what it runs, the key it is written under, and how it reads.
struct Hint {
    Command command{};
    Event key;
    /// `q reply` — the key and what it does, which is the whole of the hint,
    /// in the case `hint_bar_capitalize` asks for.
    std::string text;
};

/// The hints for whichever screen is up.
///
/// A command the layout leaves unbound is left out: there is no key to put in
/// front of it, and a label on its own would say to press nothing. Where several
/// keys run it, the shortest is the one written — `KeyMap::preferredKey()`.
std::vector<Hint> hintsOf(const AppState& state) {
    std::vector<Hint> hints;
    for (const Command command : commandsOf(state)) {
        const auto key = state.keys.preferredKey(command);
        if (!key) continue;
        // The word beside the key is the one the menu writes on a button for
        // the same command, `Commands::Info::label`: what a command is called is
        // settled in one place, and a row calling it something else would be a
        // second name for one thing. Its case is the row's own, from
        // `hint_bar_capitalize`, and the key is written to match.
        const bool capitals = state.config.hintBarCapitalize;
        const std::string_view label = Commands::of(command).label;
        hints.push_back({command, *key,
                         hintSpellingOf(*key, capitals) + " " +
                             (capitals ? std::string(label)
                                       : config::text::toLower(label))});
    }
    return hints;
}

/// What a hint is drawn in: nothing of its own while it is only standing there.
/// The row is a reminder rather than a rank of buttons, and a hint drawn to look
/// pressable would be one more thing on the screen asking to be looked at. Under
/// a click it lights up like every other button, which is the whole of how a
/// click on it is answered for.
theme::Color colorOf(bool pressed) {
    return pressed ? theme::palette.animatedButtonText : theme::palette.hintBar;
}

}  // namespace

std::string text(const AppState& state) {
    std::string row;
    for (const Hint& hint : hintsOf(state)) {
        if (!row.empty()) row += "  ";
        row += hint.text;
    }
    return row;
}

Element render(AppState& state) {
    const std::vector<Hint> hints = hintsOf(state);
    state.hintSpots.clear();

    // A screen with nothing to say leaves the rule whole, which is what closes
    // the bottom of the screen either way.
    if (hints.empty()) {
        return term::text(horizontalRule(state.width)) | color(theme::palette.separator);
    }

    // As many as the window holds, whole. A row longer than the window is
    // squeezed rather than cut — every hint losing its last letters at once,
    // `q reply` down to `q re` — and half a dozen fragments name no key and no
    // command between them. So the ones that do not fit are left off entirely,
    // from the end of the row, and what is drawn reads as it was written.
    size_t fits = 0;
    // The space on either side of the hints, which the row carries either way.
    int used = 2;
    for (const Hint& hint : hints) {
        const int cost = displayWidth(hint.text) + (fits == 0 ? 0 : 2);
        if (used + cost > state.width) break;
        used += cost;
        ++fits;
    }
    // Not even the first of them: the rule alone, as on a screen with nothing
    // to say. A window this narrow has no room to be told anything.
    if (fits == 0) {
        return term::text(horizontalRule(state.width)) | color(theme::palette.separator);
    }

    // Where in the row they stand, from `hint_bar_align`: the rest of the row
    // is the rule that closes the interface, and this is which side of the
    // hints it runs along. In the middle the odd column goes to the right-hand
    // side, so a row one column longer grows the way the text on it reads.
    const int rest = std::max(0, state.width - used);
    int before = 0;
    switch (state.config.hintBarAlign) {
        case config::HintAlign::Left: before = 0; break;
        case config::HintAlign::Center: before = rest / 2; break;
        case config::HintAlign::Right: before = rest; break;
    }
    const int after = rest - before;

    // A space on either side of the hints, as every other label set into a rule
    // in this interface carries — see `dialog_frame`, which closes its boxes the
    // same way. The two spaces between hints belong to the row rather than to
    // either of them, so that a click lands on a hint and not beside one.
    state.hintSpots.reserve(fits);
    Elements pieces;
    if (before > 0) {
        pieces.push_back(term::text(horizontalRule(before)) |
                         color(theme::palette.separator));
    }
    pieces.push_back(term::text(" "));
    for (size_t i = 0; i < fits; ++i) {
        if (i != 0) pieces.push_back(term::text("  "));
        state.hintSpots.push_back({hints[i].command, {}});
        const bool pressed =
            state.isPressed(AppState::Pressed::Hint, static_cast<uint32_t>(i));
        pieces.push_back(term::text(hints[i].text) | color(colorOf(pressed)) |
                         reflect(state.hintSpots.back().box));
    }
    pieces.push_back(term::text(" "));
    if (after > 0) {
        pieces.push_back(term::text(horizontalRule(after)) |
                         color(theme::palette.separator));
    }
    return hbox(std::move(pieces));
}

std::optional<Event> clicked(AppState& state, const Event& event) {
    const auto click = leftClick(event);
    if (!click) return std::nullopt;

    for (size_t i = 0; i < state.hintSpots.size(); ++i) {
        if (!state.hintSpots[i].box.Contain(click->x, click->y)) continue;

        // The command is taken off the row before anything else: showing the
        // click draws a frame, and `render()` builds the spots afresh every time
        // it does — which leaves nothing behind the entry it came from.
        const Command command = state.hintSpots[i].command;
        state.showClick(AppState::Pressed::Hint, static_cast<uint32_t>(i));

        // The key the hint is written under rather than a second way into the
        // command: the row says which key runs it, so a click doing anything
        // other than that key would make the row a lie. A command with no key is
        // not in the row at all, so there is always one to answer with.
        return state.keys.preferredKey(command);
    }
    return std::nullopt;
}

}  // namespace amberedit::ui::hint_bar
