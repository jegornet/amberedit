#include "ui/after_handover.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

#include "app/navigator.hpp"
#include "i18n/i18n.hpp"
#include "ui/screens/area_list_screen.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"

namespace amberedit::ui::after_handover {
namespace {

/// Every other area's counts, which is what `rescan_on_return` asks for on top
/// of the area being read.
///
/// The whole of it happens here rather than through `AppState::rescanning` and
/// the frame after, the way Ctrl-R is answered: `AreaManager::reload()` opens
/// by closing the base being read, so it has to run *before* the area is opened
/// again and never after. Deferring it to the next pass would leave a frame
/// standing between the two with nothing to read the area through.
///
/// The flag is still what puts the modal up, and the frame is asked for by hand
/// because `reload()` spends its first stretch reading the tosser config with no
/// area to name yet — without it the screen would sit blank through that.
void rescanEverything(AppState& state) {
    state.rescanning = true;
    state.redraw();
    screens::area_list::rescan(state);
    state.rescanning = false;
}

/// Where to land, given the message that was on the screen.
///
/// The UID is the anchor and the number only its fallback: a program that
/// packed the area moved every message in it, and the number would name
/// somebody else's. Nothing at or before it surviving means every survivor is
/// *newer* than what went, so the nearest one is the first — which is what
/// `indexOfUid()` answering zero has to be read as here.
///
/// A reader that stood on an empty area has neither, and an area a tosser has
/// since delivered into opens where entering it would have opened.
uint32_t landOn(AppState& state, uint32_t uid, uint32_t number) {
    if (uid != 0) {
        const uint32_t at = state.base->indexOfUid(uid);
        return at == 0 ? 1 : at;
    }
    if (number != 0) return std::min(number, state.messageCount);
    const uint32_t start =
        state.manager.startingMessage(state.currentArea, state.messageCount);
    return start == 0 ? 1 : start;
}

}  // namespace

void refresh(AppState& state) {
    // The editor is left alone entirely. Nothing on it comes off the base — the
    // draft is the user's own text — and reopening the area would put the one
    // thing that could go wrong in the way of the one thing that must not: an
    // area that will not open again drops the screen, and the half-written
    // message with it. The reader underneath is read again when the editor is
    // left, which is when it is next looked at.
    if (state.navigator.current() == app::ScreenId::Compose) return;

    // Asked of the base that is about to go, since only it can say what the
    // number on the screen means.
    const uint32_t number = state.readHeader ? state.readHeader->number : 0;
    const uint32_t uid =
        state.base != nullptr && number != 0 ? state.base->uidOf(number) : 0;
    const int scroll = state.readScroll;
    const bool revealed = state.twitRevealed;
    const std::string highlight = state.findHighlight;
    const bool hadArea = state.base != nullptr;

    // Dropped before anything reopens: `openArea()` and `reload()` both begin by
    // closing what is open, so this pointer is about to name nothing. The window
    // of headers is left standing — those are copies, and they are what keeps the
    // screen behind the rescan modal looking like the one the user left.
    state.base = nullptr;

    if (state.config.rescanOnReturn) rescanEverything(state);

    // The area list, where there is no area open and the rescan above was the
    // whole of what there was to do.
    if (!hadArea) return;

    auto opened = state.manager.openArea(state.currentArea);
    if (!opened) {
        // The one thing a program with the terminal can do that leaves nowhere
        // to go back to. Said the way the area list says it, this being the same
        // failure: see `ui/error_dialog.cpp` on why a box is allowed here at all.
        const std::string why = opened.error()->message().empty()
                                    ? std::string(_("the base could not be opened"))
                                    : opened.error()->message();
        screens::message_list::leaveArea(state);
        state.errorMessage = i18n::format(_("Cannot open the area: {0}"), {why});
        return;
    }
    state.base = *opened;

    state.messageCount = state.base->count();
    state.headers.clear();
    state.headersStart = 0;
    // A number rather than a UID, so a renumber leaves it naming somebody else's
    // message: the next search starts afresh rather than from a memory of where
    // the last one landed.
    state.lastFind = {};

    if (state.messageCount == 0) {
        screens::message_read::showEmptyArea(state);
        state.manager.refreshArea(state.currentArea);
        return;
    }

    const uint32_t at = landOn(state, uid, number);
    // Whether what comes back is the message that was on the screen, or the
    // nearest thing to it left. The two are loaded differently on purpose:
    // landing somewhere else is landing on a *place* in the area, which is what
    // `openMessage()` is for — it walks past whatever `twit_mode` says to walk
    // past, exactly as entering the area would. The message the user was already
    // reading is not walked past: they are looking at it.
    const bool same = uid != 0 && state.base->uidOf(at) == uid;
    if (same) {
        state.messageCursor = static_cast<int>(at) - 1;
        static_cast<void>(screens::message_read::loadMessage(state, at));
    } else {
        screens::message_read::openMessage(state, at);
    }

    // What belongs to the reader rather than to the message, and only where it
    // is the same message: a twit shown after all was shown for that one, and a
    // scroll offset was measured against that text. Putting either onto whatever
    // stands in its place would show a stranger's message unasked.
    //
    // Both go on before the re-wrap, which is what acts on them — and the
    // re-wrap is also what clamps the offset, so a message that came back
    // shorter needs no arithmetic here.
    if (same) {
        state.twitRevealed = revealed;
        state.findHighlight = highlight;
        state.readScroll = scroll;
        state.readLayoutWidth = 0;
        screens::message_read::relayout(state);
    }

    // Read through the base just opened, so the counts are what the area holds
    // rather than what it held before the program ran.
    state.manager.refreshArea(state.currentArea);
}

}  // namespace amberedit::ui::after_handover
