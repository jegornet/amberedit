#include "ui/hint_bar.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/text_util.hpp"
#include "encoding/text_search.hpp"
#include "i18n/i18n.hpp"
#include "ui/command_live.hpp"
#include "ui/event_util.hpp"
#include "ui/keys.hpp"
#include "ui/term/utf8.hpp"
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
    /// Nothing for the one hint that names a key rather than a command — Esc,
    /// which drops a selection and which no layout binds.
    std::optional<Command> command;
    Event key;
    /// `q reply` — the key and what it does, which is the whole of the hint,
    /// in the case `hint_bar_capitalize` asks for.
    std::string text;
};

/// A word in small letters, over the alphabets an interface can be written in.
///
/// Not `config::text::toLower`, which is ASCII by design and rightly so — it
/// reads config keys and charset names, which are ASCII by definition. A
/// command's word is not: in a Russian interface `Ответ` has to come out
/// `ответ`, and lower-casing it a byte at a time leaves it exactly as it was.
/// The table is `encoding::loweredCodePoint()`, the one the search folds with.
std::string lowered(std::string_view label) {
    std::string out;
    out.reserve(label.size());
    size_t at = 0;
    while (at < label.size()) {
        out += term::encodeUtf8(encoding::loweredCodePoint(term::decodeUtf8(label, at)));
    }
    return out;
}

/// A hint written the row's way: the key and the word, in the case
/// `hint_bar_capitalize` asks for.
Hint hintFor(const AppState& state, std::optional<Command> command, const Event& key,
             const std::string& label) {
    const bool capitals = state.config.hintBarCapitalize;
    return {command, key,
            hintSpellingOf(key, capitals) + " " + (capitals ? label : lowered(label))};
}

/// The editor's row while text is selected, which stands in place of
/// `compose_hints` for as long as the selection does: what there is to do then
/// is to do something with it, and the three hints are the whole of that.
/// Copy and cut are written under whatever keys the layout gives them; Esc is
/// the screen's own and no layout's.
std::vector<Hint> selectionHints(const AppState& state) {
    std::vector<Hint> hints;
    for (const Command command : {Command::ComposeCopy, Command::ComposeCut}) {
        const auto key = state.keys.preferredKey(command);
        if (key) hints.push_back(hintFor(state, command, *key, state.config.labelOf(command)));
    }
    hints.push_back(hintFor(state, std::nullopt, Event::Escape, _("Clear selection")));
    return hints;
}

/// The reader's row while text is being selected there, in place of
/// `reader_hints`: copying, which leaves the mode, the reply here and the reply
/// elsewhere, which quote what is selected, and leaving it without any of them.
/// Each is written under the key the layout gives the reader's command; Esc is
/// the mode's own.
std::vector<Hint> readerSelectionHints(const AppState& state) {
    std::vector<Hint> hints;
    for (const Command command :
         {Command::ReaderCopy, Command::ReaderReply, Command::ReaderReplyElsewhere}) {
        if (const auto key = state.keys.preferredKey(command)) {
            hints.push_back(hintFor(state, command, *key, state.config.labelOf(command)));
        }
    }
    hints.push_back(hintFor(state, std::nullopt, Event::Escape, _("Cancel")));
    return hints;
}

/// The hints for whichever screen is up.
///
/// A command the layout leaves unbound is left out: there is no key to put in
/// front of it, and a label on its own would say to press nothing. Where several
/// keys run it, the shortest is the one written — `KeyMap::preferredKey()`.
std::vector<Hint> hintsOf(const AppState& state) {
    if (state.navigator.current() == app::ScreenId::Compose &&
        state.composeHasSelection()) {
        return selectionHints(state);
    }
    if (state.navigator.current() == app::ScreenId::MessageRead && state.readSelect.on) {
        return readerSelectionHints(state);
    }
    std::vector<Hint> hints;
    for (const Command command : commandsOf(state)) {
        if (!commandLive(state, command)) continue;
        const auto key = state.keys.preferredKey(command);
        if (!key) continue;
        // The word beside the key is the one the menu writes on a button for
        // the same command, `AppConfig::labelOf()`: what a command is called is
        // settled in one place — a utility's own title, everything else's word
        // out of the catalog — and a row calling it something else would be a
        // second name for one thing.
        hints.push_back(hintFor(state, command, *key, state.config.labelOf(command)));
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

/// The subject of the message under the message list's cursor, and nothing on
/// any other screen. The list's Subj column is the one cut shortest when the
/// window is narrow, and the row under the list is a rule either way, so the
/// subject is written out in it whole as far as the window allows.
///
/// Read off the window of headers the list has just drawn from: the cursor is
/// always on a row of the screen, so the header is there unless the window has
/// not reached it yet, and then the rule stands alone for a frame.
std::string subjectOf(const AppState& state) {
    if (state.navigator.current() != app::ScreenId::MessageList) return {};
    const int index = state.messageCursor - state.headersStart;
    if (index < 0 || index >= static_cast<int>(state.headers.size())) return {};
    return state.headers[static_cast<size_t>(index)].subject;
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

    // The subject after the hints, in what they leave of the row and cut with an
    // ellipsis where that is not the whole of it.
    std::string subject = subjectOf(state);
    if (!subject.empty()) {
        const int gap = fits == 0 ? 0 : 2;
        const int room = state.width - used - gap;
        subject = room > 0 ? truncateToWidth(subject, room) : std::string();
        if (!subject.empty()) used += gap + displayWidth(subject);
    }

    // Nothing to say, or not even room for the first hint: the rule alone, which
    // is what closes the bottom of the screen either way.
    if (fits == 0 && subject.empty()) {
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
        state.hintSpots.push_back({hints[i].command, hints[i].key, {}});
        const bool pressed =
            state.isPressed(AppState::Pressed::Hint, static_cast<uint32_t>(i));
        pieces.push_back(term::text(hints[i].text) | color(colorOf(pressed)) |
                         reflect(state.hintSpots.back().box));
    }
    if (!subject.empty()) {
        if (fits != 0) pieces.push_back(term::text("  "));
        pieces.push_back(term::text(subject) | color(theme::palette.hintBar));
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

        // The key is taken off the row before anything else: showing the click
        // draws a frame, and `render()` builds the spots afresh every time it
        // does — which leaves nothing behind the entry it came from.
        const Event key = state.hintSpots[i].key;
        state.showClick(AppState::Pressed::Hint, static_cast<uint32_t>(i));

        // The key the hint is written under rather than a second way into the
        // command: the row says which key runs it, so a click doing anything
        // other than that key would make the row a lie. A command with no key is
        // not in the row at all, so there is always one to answer with.
        return key;
    }
    return std::nullopt;
}

}  // namespace amberedit::ui::hint_bar
