#include "ui/screens/compose_screen.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app/copy_commands.hpp"
#include "app/message_builder.hpp"
#include "config/text_util.hpp"
#include "domain/area.hpp"
#include "domain/ftn_address.hpp"
#include "domain/message.hpp"
#include "ui/attributes_dialog.hpp"
#include "ui/back_button.hpp"
#include "ui/delete_line_button.hpp"
#include "ui/edit_layout.hpp"
#include "ui/event_util.hpp"
#include "ui/import_dialog.hpp"
#include "ui/input_field.hpp"
#include "ui/menu_button.hpp"
#include "ui/menu_dialog.hpp"
#include "ui/nodelist_dialog.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/scrollbar.hpp"
#include "ui/text_editor.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::screens::compose {

using namespace term;

namespace {

/// " From : ", " To   : ", " Subj : " — the same label column the reader
/// draws, indent included, so a message being written lines up with one being
/// read.
constexpr int kLabelWidth = 8;
constexpr int kRightPad = 1;
/// "65535:65535/65535.65535": the widest a 4D address can be. Fixed rather
/// than measured, so the column does not shuffle sideways as it is typed into.
constexpr int kAddressWidth = 23;
/// What a header field will hold, in characters.
///
/// XMSG keeps 36 bytes for a name and 72 for the subject, the terminating zero
/// among them — 35 characters and 71 — and FTS-0001 says the same of both the
/// stored and the packed message. JAM is roomier (100 per subfield), but a
/// message written here has to survive whichever base it lands in and whichever
/// one it is passed on through, so the tightest of the three is the limit.
///
/// Counted in characters rather than in bytes because the message is encoded on
/// the way to the base: a Cyrillic name is two bytes here and one in CP866, and
/// a limit in bytes would refuse half a name that fits.
constexpr size_t kMaxNameChars = 35;
constexpr size_t kMaxSubjectChars = 71;

/// One column more than what fits in the field, for the cursor to stand in past
/// the last character — a box exactly as wide as its limit would scroll
/// sideways the moment it was filled.
constexpr int kMaxNameWidth = static_cast<int>(kMaxNameChars) + 1;
constexpr int kMaxSubjectWidth = static_cast<int>(kMaxSubjectChars) + 1;

int nameWidth(int screenWidth) {
    const int room = screenWidth - kRightPad - kLabelWidth - kAddressWidth;
    return std::clamp(room, 1, kMaxNameWidth);
}

/// The most the field will take, or 0 for one that is not limited this way: an
/// address is bounded by what parses as an address, and the attributes are a
/// button with no text at all.
size_t fieldLimit(int field) {
    switch (field) {
        case kFromName:
        case kToName: return kMaxNameChars;
        case kSubject: return kMaxSubjectChars;
        default: return 0;
    }
}

/// What the attributes button says over a message carrying none. An empty pair
/// of brackets would say the same thing about the message and nothing about the
/// button; this says both.
constexpr const char* kNoAttrsLabel = "Attrs...";

/// The field's text, so that one piece of code can walk all five. Never asked
/// of the attributes button, which has none.
std::string& valueOf(app::ComposeFields& fields, int field) {
    switch (field) {
        case kFromName: return fields.fromName;
        case kFromAddr: return fields.fromAddr;
        case kToName: return fields.toName;
        case kToAddr: return fields.toAddr;
        default: return fields.subject;
    }
}

const std::string& valueOf(const app::ComposeFields& fields, int field) {
    return valueOf(const_cast<app::ComposeFields&>(fields), field);
}

/// One field of the header — or one line of the text, which is edited the same
/// way — `width` columns wide and written in `tint`.
///
/// `ui::inputField()` is the field itself, here and in the import dialog alike.
/// What this adds is the `spot`: where the field landed and the byte its
/// leftmost column shows, which together are what a click is answered against.
/// Where it landed is read off the frame rather than worked out twice.
///
/// `filler` underscores the room the field has left. The header fields ask for
/// it; the Date and Recd stamps and the row of message text under the cursor do
/// not, being shown or written rather than asked for.
Element field(const std::string& value, size_t cursor, int width, bool active,
              theme::Color tint, std::optional<theme::Color> filler = std::nullopt,
              AppState::ComposeSpot* spot = nullptr) {
    if (spot == nullptr) return inputField(value, cursor, width, active, tint, filler);
    return inputField(value, cursor, width, active, tint, filler, &spot->origin) |
           reflect(spot->box);
}

/// One row: the label, the name column and — where the row has one — the
/// address column, hard against it. The two are fields now, each on a fill of
/// its own, and the edge where one ends and the next begins is what a column of
/// blank between them used to be there to draw.
Element row(const std::string& label, Element name, Element address) {
    Elements cells{
        text(" " + padRight(label, 4) + " : ") | bold | color(theme::palette.header),
        std::move(name)};
    if (address) cells.push_back(std::move(address));
    return hbox(std::move(cells));
}

/// What the message's attributes say, in the short forms the reader shows them
/// in — "[Uns Pvt Loc]" — or nothing at all where it carries none, an empty pair
/// of brackets being no more informative there than a blank column.
///
/// The same `messageAttributes()` the reader calls, on the same attributes, so a
/// message reads the same being written as it will read being read — the virtual
/// `Uns` included, which is not a bit but MSGLOCAL without MSGSENT and is
/// therefore as true of the message on the screen as of the one in the base.
std::string attributesText(uint32_t attributes) {
    const std::vector<std::string> names = domain::messageAttributes(attributes);
    if (names.empty()) return {};

    std::string out = "[";
    for (size_t i = 0; i < names.size(); ++i) out += (i == 0 ? "" : " ") + names[i];
    return out + "]";
}

/// The Date row's right-hand column: the message's attributes, under the
/// addresses where the reader shows the same thing, and standing as the button
/// that opens the dialog they are set in.
///
/// **The attributes are the button.** A separate one beside them could only
/// repeat what they already say, and it took a column of its own to do it;
/// pointing at what is to be changed is how everything else on this screen is
/// reached. A message carrying none says "Attrs..." instead — a button with
/// nothing on it yet, rather than a blank column with a dialog hidden behind
/// it.
///
/// Out of focus it wears `input_field`, the fill the header's own boxes wear
/// when the typing is elsewhere: it is one of the stops of that block and reads
/// as one, rather than as a value written on the screen beside them. In focus it
/// takes `focused_field`/`focused_text`, the fields' own pair, as a stop in the
/// same ring has to.
///
/// The fill is only as wide as what the attributes say, where a field's is the
/// width of its column: a field is a box waiting for something and has to show
/// how much room it has, and this one is not typed into.
Element attributesColumn(AppState& state, int width, bool focused) {
    const std::string carried = attributesText(state.compose.attributes);
    const std::string shown =
        truncateToWidth(carried.empty() ? kNoAttrsLabel : carried, width);

    state.changeAttributesBox = Box::Nowhere();
    if (shown.empty()) return text(padRight("", std::max(0, width)));

    const bool pressed = state.isPressed(AppState::Pressed::ChangeAttributes);
    auto button = text(shown);
    if (focused) {
        button = std::move(button) | bold |
                 color(pressed ? theme::palette.animatedButtonText
                               : theme::palette.focusedText) |
                 bgcolor(theme::palette.focusedField);
    } else {
        button = std::move(button) |
                 color(pressed ? theme::palette.animatedButtonText
                               : theme::palette.inputText) |
                 bgcolor(theme::palette.inputField);
    }

    // The button is only as wide as what it says: the fill beside it is the
    // row's, not the button's, so a click past them is a click on the row.
    const int used = displayWidth(shown);
    return hbox({std::move(button) | reflect(state.changeAttributesBox),
                 text(std::string(static_cast<size_t>(std::max(0, width - used)), ' '))});
}

/// The four rows of the header block, over the message's text.
///
/// `editing` is whether the typing goes into them: the field the cursor is in
/// carries it, and none of them does while the text below has it.
Elements headerRows(AppState& state, bool editing) {
    // Nowhere until the row is drawn, so that the one echomail leaves out —
    // the To address — answers no click at all.
    state.composeFieldSpots.assign(kFieldCount, AppState::ComposeSpot{});

    const int names = nameWidth(state.width);
    // A field that is typed into is drawn on a fill the width of its column, so
    // that it reads as a box asking for something before anything is in it: the
    // block is otherwise five values written on a screen, with nothing about
    // them saying which of the two blocks on this screen may be changed. Idle,
    // that is `input_field` with `input_text` on it; the one the typing is in
    // takes `focused_field`/`focused_text` instead — the same pair the
    // attributes button beside them takes, so whatever the typing is on wears
    // one color everywhere.
    const auto cell = [&](int which, int width) {
        const bool focused = editing && state.composeField == which;
        return field(valueOf(state.compose, which), state.composeCursor, width, focused,
                     focused ? theme::palette.focusedText : theme::palette.inputText,
                     fieldFiller(theme::palette.inputFiller),
                     &state.composeFieldSpots[static_cast<size_t>(which)]) |
               bgcolor(focused ? theme::palette.focusedField : theme::palette.inputField);
    };
    // What is left of the row once the labels and the names are taken off —
    // wider than the address column above it, since the names stop at
    // kMaxNameWidth and a roomy window leaves the rest over.
    const int trailing = std::max(1, state.width - kRightPad - kLabelWidth - names);

    // The Date row the reader draws, over a message that has no stamp of its
    // own yet: what is being written is written now, read afresh on every
    // frame. Shown rather than typed into — the message is stamped when it is
    // stored, not from here, which is why the row is not one of the fields the
    // cursor walks. The Recd row under it, where `show_recd_date` asks for one,
    // is the same clock read the same way: a message being written arrives here
    // as it is stored, and the row is there so that the block goes on being the
    // reader's field for field.
    //
    // A message being changed is stamped afresh when it is stored, so the row
    // is the clock there too — it says what the message will be dated by, not
    // what it was dated by when the editor opened on it.
    //
    // The zone `%z` writes here is the clock here, which is the offset the
    // TZUTC of this message will state: the reader shows a message by the zone
    // it says it was written in, and the editor is where this one is being
    // written.
    // FTS-4008 leaves the plus off a positive offset and `%z` writes one, so
    // the sign is put back here: a message being written must have its zone
    // spelled the way the reader spells one that has been stored.
    const std::time_t when = std::time(nullptr);
    std::tm broken{};
    localtime_r(&when, &broken);
    const auto offsetMinutes = static_cast<int>(broken.tm_gmtoff / 60);
    const std::string zone =
        (offsetMinutes < 0 ? "" : "+") + app::tzutcOffset(offsetMinutes);
    const std::string now =
        app::localStamp(when).format(state.config.readerDateTimeFormat, zone);
    // The arrival stamp is given no zone, here as in the reader: when a message
    // arrived is read off this system's clock, which is not something the
    // message states, so `%z` writes nothing there on either screen.
    const std::string arrived =
        app::localStamp(when).format(state.config.readerDateTimeFormat);

    Elements rows{
        row("From", cell(kFromName, names), cell(kFromAddr, kAddressWidth)),
        // Echomail is addressed to the area, so the row carries a name and
        // nothing else — the same as the reader shows for it.
        row("To", cell(kToName, names),
            state.compose.netmail ? cell(kToAddr, kAddressWidth) : Element{}),
        // The subject runs across both columns, as it does in the reader — but
        // no further than what it will hold: a box stretching across a wide
        // window would offer room the base has none for.
        row("Subj", cell(kSubject, std::min(names + trailing, kMaxSubjectWidth)),
            Element{}),
        // The stamp in the block's own color, and on no fill: it is the one
        // value here that is shown rather than typed into, and the fills above
        // are what say which of them the typing may go to.
        row("Date", field(now, 0, names, false, theme::palette.header),
            attributesColumn(state, trailing,
                             editing && state.composeField == kAttributes)),
    };
    // In the block's own color and on no fill, as the Date row above it is: the
    // two are a pair of stamps, and the reader draws them the same way.
    if (state.recdRowShown()) {
        rows.push_back(row("Recd", field(arrived, 0, names, false, theme::palette.header),
                           Element{}));
    }
    return rows;
}

/// What the screen is doing, for its title row. The area named is the one the
/// message is going into, which for a moved reply is not the one underneath —
/// that being the whole point of naming it here.
std::string titleText(const AppState& state) {
    // A reply following the message's own AREA: line is not a moved one: it is
    // going where the message says it belongs, and the area named beside this
    // word is that echo. Nothing was moved but the screen.
    const bool movedByHand = state.compose.moved && !state.compose.direct;
    const std::string what = state.compose.changing  ? "change"
                             : state.compose.forward ? "forward"
                             : movedByHand           ? "moved reply"
                             : state.compose.reply   ? "reply"
                                                     : "new message";
    return " " + state.composeArea().tag + " — " + what;
}

/// The frame both compose screens are drawn in: the title with the way back
/// beside it and the way into the menu on the other side, and the block passed
/// in under them.
///
/// The block closes itself off: the rule under it belongs to what is being
/// drawn rather than to the frame.
Element chrome(const AppState& state, Elements body) {
    auto rule = [&] {
        return text(horizontalRule(state.width)) | color(theme::palette.separator);
    };

    // The two corners — the same buttons the reader carries, in the same
    // corners, taking the same two rows: the way back on the left, and the menu
    // button on the right.
    const bool back = state.backButtonShown();
    const bool menu = state.composeMenuShown();
    const int titleRoom = std::max(1, state.width - (back ? back_button::kWidth : 0) -
                                          (menu ? menu_button::kWidth : 0));
    auto title = text(truncateToWidth(titleText(state), titleRoom)) | bold |
                 color(theme::palette.tableHeader);

    const bool pressedBack = state.isPressed(AppState::Pressed::Back);
    const bool pressedMenu = state.isPressed(AppState::Pressed::MenuButton);
    // Each button stands a column clear of the rule under the title, so it reads
    // as a thing beside it rather than a piece of it.
    const int ruleWidth = std::max(0, state.width - (back ? back_button::kWidth + 1 : 0) -
                                          (menu ? menu_button::kWidth + 1 : 0));

    Elements content;
    if (back || menu) {
        Elements titleRow;
        Elements ruleRow;
        if (back) {
            titleRow.push_back(back_button::topRow(pressedBack));
            ruleRow.push_back(back_button::bottomRow(pressedBack));
        }
        titleRow.push_back(std::move(title));
        ruleRow.push_back(
            text((back ? " " : "") + horizontalRule(ruleWidth) + (menu ? " " : "")) |
            color(theme::palette.separator));
        if (menu) {
            // The filler is what holds the button against the right edge
            // whatever the title came to, and it is a child of this row rather
            // than of the title: an hbox does not carry a child's appetite for
            // room up to its own parent.
            titleRow.push_back(filler());
            titleRow.push_back(menu_button::topRow(pressedMenu));
            ruleRow.push_back(menu_button::bottomRow(pressedMenu));
        }
        content.push_back(hbox(std::move(titleRow)));
        content.push_back(hbox(std::move(ruleRow)));
    } else {
        content.push_back(std::move(title));
        content.push_back(rule());
    }
    for (auto& element : body) content.push_back(std::move(element));

    // The block sits at the top and the rest of the window is the room the text
    // takes, as on every other screen.
    for (int i = static_cast<int>(content.size()); i < state.height; ++i) {
        content.push_back(text(""));
    }
    return vbox(std::move(content));
}

/// Moves to the next field in `step`'s direction, skipping the To address
/// where there is none. Returns kFieldCount when the header is done with.
int fieldAfter(const AppState& state, int step) {
    int next = state.composeField + step;
    if (next == kToAddr && !state.compose.netmail) next += step;
    return std::clamp(next, 0, static_cast<int>(kFieldCount));
}

void moveTo(AppState& state, int field) {
    state.composeField = field;
    // The cursor lands at the end of what is already there, which is where
    // typing carries on from. The attributes button holds no text for it to
    // stand in, and is drawn as a whole rather than character by character.
    state.composeCursor = field == kAttributes ? 0 : valueOf(state.compose, field).size();
}

/// Hands the typing to the header, on the field named.
void enterHeader(AppState& state, int field) {
    state.composeInHeader = true;
    moveTo(state, field);
}

/// Which address field the header cannot be left with, or -1 when it can be.
///
/// The sender's address is what a message is written from and what its MSGID is
/// made of, so every message needs one that parses; in netmail the recipient's
/// is the whole point of the message, and outside it the field addresses nobody
/// and is not even shown. Missing and malformed are one case here: both leave
/// nothing to write the message from, and `FtnAddress::parse` answers for both.
int badAddressField(const AppState& state) {
    if (!domain::FtnAddress::parse(state.compose.fromAddr)) return kFromAddr;
    if (state.compose.netmail && !domain::FtnAddress::parse(state.compose.toAddr)) {
        return kToAddr;
    }
    return -1;
}

/// Leaves the To address, choosing the AKA to write from along the way.
/// false keeps the cursor where it is: the address does not parse, and the
/// choice is made on the address.
bool leaveToAddr(AppState& state) {
    const std::string& typed = state.compose.toAddr;
    if (typed.find_first_not_of(" \t") == std::string::npos) return true;

    const auto dest = domain::FtnAddress::parse(typed);
    if (!dest) return false;

    // The AKA `akamatch` picks for the destination, which the From row above
    // shows the moment the cursor leaves this one. Off the file's own config
    // rather than the area's: `akamatch` is not a setting an area group may
    // state, so the two would answer alike, and reading this one says which.
    const auto sender = app::senderFor(state.config, *dest);
    if (sender) state.compose.fromAddr = *sender;
    return true;
}

/// Everything the message builder needs, gathered off the state. The message
/// being answered or forwarded is the one in the reader — which is where both
/// are begun.
app::BuildRequest buildRequest(AppState& state) {
    const std::time_t now = std::time(nullptr);
    std::tm broken{};
    localtime_r(&now, &broken);

    // A new message carries nothing of what is on screen; a reply and a forward
    // both carry the message being read, and `compose.forward` is what says
    // which of the two it is being carried for.
    const bool carries = state.compose.reply || state.compose.forward;
    return app::BuildRequest{
        // The settings of the area it is going into — the origin, the tearline,
        // the template and the charset it is written in are all that area's.
        // Owned by the state, so the reference this keeps outlives the call.
        state.composeConfig(),
        // Where the message goes, which is the area picked in the dialog where
        // one was picked and the area being read otherwise.
        state.composeArea(),
        state.compose,
        carries && state.readHeader ? &*state.readHeader : nullptr,
        carries && state.readBody ? &*state.readBody : nullptr,
        // The area that message was read in, named only when this one is going
        // somewhere else — @oecho is the area itself when it is not. A reply
        // following the message's own AREA: line names nothing either: as far
        // as the message is concerned it is being answered in the echo it was
        // posted to, and what the area it was *read* in happens to be called is
        // this system's business and not the network's.
        carries && state.composeGoesElsewhere() && !state.compose.direct
            ? &state.currentArea
            : nullptr,
        now,
        static_cast<int>(broken.tm_gmtoff / 60),
        // The reader's kludges, which decide whether the quote or the forward
        // carries them: a message is answered the way it was being read.
        state.showKludges,
    };
}

/// The text as it is drawn: one entry per row of the screen, a line too wide
/// for the window taking several of them — and, with them, the width they were
/// broken at and whether the scrollbar stands beside them.
///
/// Worked out afresh wherever it is wanted rather than kept on the state: it
/// follows from the lines and the width, both of which are there already, and a
/// copy of it would only be a second thing to keep true.
struct TextLayout {
    std::vector<EditRow> rows;
    /// What the lines were broken at: the window, less the three columns the
    /// delete-line button stands in where there is one, and less the column the
    /// bar takes where there is a bar and no button to stand it under.
    int width{1};
    bool scrollbar{false};
};

/// The layout of the text, the scrollbar decided along with it.
///
/// Laid out twice for the same reason the reader's `relayout()` does it: the
/// bar costs the very column that decides whether it is needed. The full width
/// first, so that a message that fits keeps the whole window; one that does not
/// is broken a column narrower — which can only make it longer, never short
/// enough to fit again, so the answer does not flip back.
///
/// The reader's `b` and `reader_scrollbar` have no say here. They are about a
/// message being read, where the bar is one thing among several the reader can
/// be asked to leave off; in the editor it is what says there is more of the
/// message than the window is showing, and there is nothing else on the screen
/// that says it.
[[nodiscard]] TextLayout textLayout(const AppState& state) {
    // The delete-line button's three columns come off every row, whatever the
    // cursor is on and whether or not the message overflows: it walks the
    // message as the cursor does, and a width that changed with it would rewrap
    // the whole message at every keystroke.
    const int reserved = state.composeDeleteLineShown() ? delete_line_button::kWidth : 0;

    TextLayout layout;
    layout.width = std::max(1, state.width - reserved);
    layout.rows = layoutRows(state.edit, layout.width);
    if (static_cast<int>(layout.rows.size()) <= std::max(1, editorRows(state)))
        return layout;

    layout.scrollbar = true;
    // The bar goes under the button, in the last of the columns already taken —
    // so where there is a button the message costs nothing more by overflowing,
    // and there is nothing here to lay out a second time.
    if (reserved > 0) return layout;
    layout.width = std::max(1, state.width - 1);
    layout.rows = layoutRows(state.edit, layout.width);
    return layout;
}

/// Keeps the cursor on screen after whatever just moved it. The scroll counts
/// rows of the screen, not lines of the message: a line four rows tall is
/// scrolled through a row at a time, as it is read.
void scrollToCursor(AppState& state) {
    const int rows = std::max(1, editorRows(state));
    const TextLayout layout = textLayout(state);
    const auto at =
        static_cast<int>(rowOfCursor(layout.rows, state.edit.row, state.edit.col));

    state.editScroll = std::min(state.editScroll, at);
    if (at >= state.editScroll + rows) state.editScroll = at - rows + 1;
    state.editScroll = std::max(0, state.editScroll);
}

/// The cursor `delta` rows down the screen, negative for up — what the arrows
/// and the page keys move by. Rows rather than lines, so the cursor goes down a
/// long line the way the eye does.
void moveRows(AppState& state, int delta) {
    const TextLayout layout = textLayout(state);
    moveByRows(state.edit, layout.rows, delta, layout.width);
}

/// The wheel over the message: the text moves a row per notch, as the reader's
/// body does, and the cursor comes along when the window has passed it by.
///
/// It stays where it is for as long as it is on the screen — the point of
/// scrolling is to look elsewhere without losing the place being written — and
/// is carried onto the nearest row still showing when it would otherwise be
/// left behind. It has to end up somewhere on screen: the cursor is what the
/// frame scrolls to, so one left off it would have the next frame put the text
/// straight back where it was.
void wheelScroll(AppState& state, int delta) {
    const int rows = std::max(1, editorRows(state));
    const TextLayout layout = textLayout(state);
    const int bottom = std::max(0, static_cast<int>(layout.rows.size()) - rows);

    const int was = state.editScroll;
    state.editScroll = std::clamp(state.editScroll + delta, 0, bottom);
    if (state.editScroll == was) return;

    const auto at =
        static_cast<int>(rowOfCursor(layout.rows, state.edit.row, state.edit.col));
    const int onto = std::clamp(at, state.editScroll, state.editScroll + rows - 1);
    if (onto != at) moveByRows(state.edit, layout.rows, onto - at, layout.width);
}

/// The header as it stands, field by field — what a template's words were
/// chosen against, so that a block left exactly as it was found asks for
/// nothing to be built again.
std::vector<std::string> headerSnapshot(const AppState& state) {
    std::vector<std::string> fields;
    // The fields that are typed into, which are the stops before the button:
    // the attributes are the message's, but no template line was ever chosen
    // for them, so setting one rebuilds nothing.
    fields.reserve(kAttributes);
    for (int i = 0; i < kAttributes; ++i) fields.push_back(valueOf(state.compose, i));
    return fields;
}

/// Fills the editor with what the template makes of this message, and puts the
/// cursor on the line the template's @position names — for a forward as for
/// every other message.
///
/// A forward once opened at the bottom of the text instead, on the reasoning
/// that what it carries is already written and what is left to write goes under
/// it. That is only true of a template whose @position is its last word: the one
/// GoldED ships signs the message off with @CFName below it, and a forward
/// opening at the end of the text put the typing under the user's own name.
/// @position is where the template says the writing starts, and a forward is not
/// the one message that knows better. The bare @position is the one that
/// answers here: `@quoted@position` stands on a line a forward never carries.
void fillFromTemplate(AppState& state) {
    const auto start = app::startingText(buildRequest(state));
    state.edit.lines = start.lines;
    if (state.edit.lines.empty()) state.edit.lines.emplace_back();
    // What the template said, kept beside what the user has since made of it:
    // as long as the two are the same, the header may still rewrite the text.
    state.composeStartText = state.edit.lines;
    state.composeStartHeader = headerSnapshot(state);

    const auto lines = static_cast<int>(state.edit.lines.size());
    state.edit.row = std::clamp(start.cursorLine, 0, lines - 1);
    state.edit.col = state.edit.line().size();
    // Only far enough to show the cursor, as everywhere else. In a forward that
    // is nearly the whole way down anyway — the message being passed on stands
    // above the greeting — so the end of what is carried comes on screen with
    // the line the writing starts on.
    state.editScroll = 0;
    scrollToCursor(state);
}

/// Opens the compose screen on a message whose fields have just been prefilled,
/// with the template already expanded into the text below them.
void openCompose(AppState& state, bool inHeader) {
    state.navigator.push(app::ScreenId::Compose);
    state.composeInHeader = inHeader;
    fillFromTemplate(state);
}

/// Opens it on text that is already written — the message being changed, which
/// no template has anything to add to. The cursor starts at the top of it: what
/// is being changed may be anywhere in the message, and the first line is where
/// reading it starts.
void openWithText(AppState& state, std::vector<std::string> lines) {
    state.navigator.push(app::ScreenId::Compose);
    state.composeInHeader = false;
    state.edit.lines = std::move(lines);
    if (state.edit.lines.empty()) state.edit.lines.emplace_back();
    // Nothing to expand again, and nothing to compare against: refreshTemplate()
    // is held off a message being changed altogether. The two are still set, so
    // that what is on the state describes what is on the screen.
    state.composeStartText = state.edit.lines;
    state.composeStartHeader = headerSnapshot(state);
    state.edit.row = 0;
    state.edit.col = 0;
    state.editScroll = 0;
}

/// Expands the template again where nothing has been written over it yet.
///
/// The header is filled in after the text is now, and a template greets the
/// recipient by name and closes the message with an origin carrying the
/// sender's address. Leaving the header is where those answers are known, so
/// the words the template chose are chosen again — unless the user has typed
/// among them, in which case the message is theirs and is left alone.
void refreshTemplate(AppState& state) {
    // Never over a message being changed: what is in the editor is the message
    // itself, and a template expanded over it would throw the message away —
    // the one thing that must not happen here.
    if (state.compose.changing) return;
    if (headerSnapshot(state) == state.composeStartHeader) return;
    if (state.edit.lines != state.composeStartText) return;
    fillFromTemplate(state);
}

/// Gives the cursor back to the text, the header having been filled in.
void leaveHeader(AppState& state) {
    state.composeInHeader = false;
    refreshTemplate(state);
    scrollToCursor(state);
}

/// Back out of the compose screen, onto the reader underneath.
void leaveEditor(AppState& state) {
    state.edit = TextBuffer{};
    state.composeStartText.clear();
    state.composeStartHeader.clear();
    state.editScroll = 0;
    state.composeInHeader = false;
    // Whatever was being changed is not being changed any more: `changing` is
    // what saving reads to decide which door the message leaves by, and the
    // message it named is gone from under the editor as soon as this returns.
    state.compose.changing = false;
    state.changeNumber = 0;
    state.changeKept = {};
    // The commands went with the message. What is left of the run is the answer
    // to a question about a message that has been stored, and the next one is
    // asked about afresh.
    state.copyRun.reset();
    state.navigator.pop();
}

/// Stores a message written into another area — a reply moved into one, or a
/// message forwarded there — and puts the reader back where it was.
///
/// Only one base is ever open, so the target's takes the place of the one being
/// read and the reader's own is opened again straight after. Nothing on the
/// screen underneath comes off the base while it is away — the header and the
/// body being read are copies, and so are the list's headers — so the swap
/// leaves the reader exactly as it was, scroll and all.
///
/// false means the message did not go in and the editor is to keep it: either
/// the area picked would not open, or its base refused the message.
bool storeElsewhere(AppState& state, const domain::MessageDraft& draft) {
    const domain::AreaConfig target = state.composeArea();
    const domain::AreaConfig source = state.currentArea;

    // The source's base is closed by opening another, so nothing may be left
    // pointing at it in between.
    state.base = nullptr;
    uint32_t written = 0;
    if (const auto into = state.manager.openArea(target)) {
        if (const auto number = (*into)->write(draft)) {
            written = *number;
            // The area list counts it while the base is still open — one message
            // more in an area nobody has read, so one unread more as well.
            state.manager.refreshArea(target);
        }
    }

    // Back where the user was, whether or not the message went in.
    state.base = state.manager.openArea(source).value_or(nullptr);
    if (state.base == nullptr) {
        // The area that was open a moment ago will not open again. There is
        // nothing left underneath to come back to, so this ends on the area
        // list — and the message, stored or not, is not worth an editor over a
        // reader showing an area that is gone.
        message_list::leaveArea(state);
        return true;
    }
    return written != 0;
}

// --- the CC: and XC: commands ------------------------------------------------

/// The area the copies a `CC:` line asks for are written into, or nothing where
/// there is nowhere for them to go — in which case those lines are not commands
/// at all and stay in the message as the text they were typed as.
///
/// Netmail answers with itself: a copy of a netmail is another netmail beside
/// the one being written. An echo answers with the netmail area its
/// `reply_to_area` names, that being where the answers to the echo are written
/// and so where a message addressed to one person belongs. An echo naming none
/// has nowhere to put such a message, and a local area is not a place anything
/// leaves by.
std::optional<domain::AreaConfig> carbonArea(AppState& state) {
    const domain::AreaConfig& area = state.composeArea();
    if (area.kind == domain::AreaKind::Netmail) return area;
    if (area.kind == domain::AreaKind::Local) return std::nullopt;

    const std::string& tag = state.composeConfig().replyToArea;
    if (tag.empty()) return std::nullopt;
    for (const auto& entry : state.manager.areas()) {
        if (!config::text::iequals(entry.config.tag, tag)) continue;
        // The area named has to be a netmail one. `reply_to_area` also says
        // where the reply dialog opens, and that may be any area at all; a
        // carbon copy is addressed to a person, which is what netmail is for.
        return entry.config.kind == domain::AreaKind::Netmail
                   ? std::optional<domain::AreaConfig>(entry.config)
                   : std::nullopt;
    }
    return std::nullopt;
}

/// The commands in the text that are commands here.
///
/// A `CC:` line in an area whose copies have nowhere to go is left out: it is
/// not carried out, it is not taken out of the message, and nothing is said
/// about it — that is what a `CC:` line in a local echo is, a line somebody
/// wrote.
std::vector<app::CopyCommand> commandsIn(AppState& state,
                                         const std::vector<std::string>& lines) {
    std::vector<app::CopyCommand> commands =
        app::findCopyCommands(lines, state.config.configDir);
    if (carbonArea(state)) return commands;

    commands.erase(std::remove_if(commands.begin(), commands.end(),
                                  [](const app::CopyCommand& command) {
                                      return command.kind == app::CopyKind::Carbon;
                                  }),
                   commands.end());
    return commands;
}

/// Marks a command line as one to leave standing in the message. Twice over the
/// same line says the same thing once: a line naming three people of whom two
/// are unknown is one line, and it stays.
void keepLine(AppState::CopyRun& run, size_t line) {
    if (std::find(run.keep.begin(), run.keep.end(), line) == run.keep.end()) {
        run.keep.push_back(line);
    }
}

/// Whose address that is, as the nodelist has it. `Sysop` where it lists none —
/// the name a message to a node that has no name to it has always gone to, and
/// better than a header addressed to nobody at all.
std::string sysopAt(AppState& state, const domain::FtnAddress& address) {
    if (const nodelist::NodelistDb* db = state.nodelist(); db != nullptr) {
        if (const auto at = db->find(address)) return db->entry(*at).sysop;
    }
    return "Sysop";
}

/// What became of one `CC:` token.
enum class Lookup {
    Found,    ///< a recipient, whole
    Ask,      ///< a name the nodelist has more than one answer to, or none
    Unknown,  ///< nothing here can say who that is
};
struct CarbonLookup {
    Lookup outcome{Lookup::Unknown};
    app::CarbonCopy copy;
};

/// Who a `CC:` token names, by the same three answers the To row is filled in
/// by and in the same order: an `address_macro` first, then the address itself
/// where the token holds one, then the nodelist for a name.
///
/// `area` is the AKA the copies go out under, which is what finishes an address
/// written in part: `/1234` is the node in this AKA's net.
CarbonLookup resolveCarbon(AppState& state, const app::CopyToken& token,
                           const domain::FtnAddress& area) {
    CarbonLookup found;
    found.copy.hidden = token.hidden;

    // The whole of the token has to be the macro, as it does in the To row: a
    // macro found inside a name would make every macro a word nobody could
    // write to.
    if (const config::AddressMacro* macro = state.config.addressMacroFor(token.text)) {
        found.outcome = Lookup::Found;
        found.copy.name = macro->name;
        found.copy.address = macro->address;
        return found;
    }

    const app::WrittenRecipient written = app::readRecipient(token.text);
    if (!written.address.empty()) {
        const auto address = app::completeAddress(written.address, area);
        // An address that says too little for the area to finish it names
        // nobody, and there is nothing to look up: a name is what the nodelist
        // is asked about, and this token holds an address.
        if (!address) return found;
        found.outcome = Lookup::Found;
        found.copy.address = *address;
        found.copy.name = written.name.empty() ? sysopAt(state, *address) : written.name;
        return found;
    }

    // A name, which only the nodelist can answer for. Without one there is
    // nothing to ask and nothing to ask with.
    const nodelist::NodelistDb* db = state.nodelist();
    if (db == nullptr) return found;

    const std::vector<size_t> matches =
        db->findBySysop(token.text, 0, nodelist::NodelistDb::SysopOrder::Relevance);
    if (matches.size() == 1) {
        const nodelist::NodeEntry node = db->entry(matches.front());
        found.outcome = Lookup::Found;
        found.copy.name = node.sysop;
        found.copy.address = node.address;
        return found;
    }
    // The whole name, spelled as the nodelist spells it, and only one node
    // holding it: `Ivan Ivanov` among four Ivanovs is that Ivan Ivanov.
    size_t whole = 0;
    size_t at = 0;
    for (const size_t index : matches) {
        if (config::text::iequals(db->entry(index).sysop, token.text)) {
            ++whole;
            at = index;
        }
    }
    if (whole == 1) {
        const nodelist::NodeEntry node = db->entry(at);
        found.outcome = Lookup::Found;
        found.copy.name = node.sysop;
        found.copy.address = node.address;
        return found;
    }

    // Several nodes, or none: the user is asked, exactly as Enter on a half
    // filled-in To row asks.
    found.outcome = Lookup::Ask;
    return found;
}

/// Whether the copy would go to exactly the recipient the message itself is
/// addressed to — both the name, read without regard to case, and the address.
/// Either of them differing makes it somebody else, and somebody else gets a
/// copy.
bool addressesTheSame(const app::ComposeFields& fields, const app::CarbonCopy& copy) {
    if (!config::text::iequals(fields.toName, copy.name)) return false;
    const auto addressed = domain::FtnAddress::parse(fields.toAddr);
    return addressed && addressed->same4D(copy.address);
}

void resolveCopies(AppState& state);

/// The commands carried out: the lists built, the text rewritten round them,
/// and the message stored.
void finishCopies(AppState& state) {
    AppState::CopyRun& run = *state.copyRun;
    const config::AppConfig& config = state.composeConfig();

    // The one value that leaves the command line where it was written. It is
    // decided here rather than while the tokens are walked: what a line came to
    // is one question and what the message keeps of it is another.
    for (const auto& command : run.commands) {
        const bool raw = command.kind == app::CopyKind::Carbon
                             ? config.carbonList == config::CarbonList::Keep
                             : config.crosspostList == config::CrosspostList::Raw;
        if (raw || command.tokens.empty()) keepLine(run, command.line);
    }

    // "Originally in" says where the message was written, and is worth saying
    // only where it also went somewhere else. A mask written with a `#` that
    // covered this very area is the way to ask for the crossposting without
    // that line.
    const std::string originally =
        !run.crossposts.empty() && !run.currentHidden ? state.composeArea().tag : "";

    const std::vector<std::string> carbon =
        app::carbonLines(run.carbons, config.carbonList, config.quoteMargin);
    const std::vector<std::string> crossposts = app::crosspostLines(
        run.crossposts, originally, config.crosspostList, config.quoteMargin);
    if (config.carbonList == config::CarbonList::Hidden) {
        run.kludges = app::carbonKludges(run.carbons);
    }

    run.text = app::rewriteCopyCommands(trimmedLines(state.edit), run.commands, run.keep,
                                        carbon, crossposts);
    saveMessage(state);
}

/// Walks the commands from wherever the last answer left off, and ends by
/// storing the message.
///
/// It returns early exactly once: on a `CC:` token the nodelist cannot answer
/// for outright, having put the box up. `useCarbonCopy()` writes the answer down
/// and calls this again, so the walk is one loop however many times it is
/// interrupted.
void resolveCopies(AppState& state) {
    AppState::CopyRun& run = *state.copyRun;

    // The AKA the copies are written under, which is what finishes an address
    // written in part. Netmail or the echo's `reply_to_area`; an echo whose
    // `CC:` lines are text has none of them among its commands at all.
    domain::FtnAddress carbonAka;
    std::optional<domain::AreaConfig> target = carbonArea(state);
    if (target) {
        const config::AppConfig config = state.config.effectiveFor(*target);
        if (const auto parsed =
                domain::FtnAddress::parse(app::ownAddress(config, *target))) {
            carbonAka = *parsed;
        }
    }

    // The areas a crosspost mask is matched against — every area there is, the
    // netmail ones dropped where the mask is applied.
    std::vector<domain::AreaConfig> areas;
    areas.reserve(state.manager.areas().size());
    for (const auto& entry : state.manager.areas()) areas.push_back(entry.config);

    while (run.command < run.commands.size()) {
        const app::CopyCommand& command = run.commands[run.command];
        if (run.token == 0 && !command.error.empty()) {
            // A file that could not be read is a list of recipients nobody has
            // seen. The line stays and the box afterwards says why.
            keepLine(run, command.line);
            run.unresolved.push_back(command.error);
        }
        if (run.token >= command.tokens.size()) {
            ++run.command;
            run.token = 0;
            continue;
        }

        const app::CopyToken& token = command.tokens[run.token];
        if (command.kind == app::CopyKind::Crosspost) {
            const app::MaskResult result =
                app::addCrossposts(token, areas, state.composeArea(), run.crossposts);
            if (!result.matched) {
                // A mask that covered no area at all: the line stays, since
                // nothing was crossposted on its account. One that covered an
                // echo another mask had already named is no failure — it is
                // two ways of saying the same echo.
                keepLine(run, command.line);
                run.unresolved.push_back(token.text);
            }
            if (result.current && token.hidden) run.currentHidden = true;
            ++run.token;
            continue;
        }

        const CarbonLookup found = resolveCarbon(state, token, carbonAka);
        if (found.outcome == Lookup::Ask) {
            nodelist_dialog::openFor(
                state, AppState::NodelistView::Purpose::PickCarbonCopy, token.text);
            return;
        }
        if (found.outcome == Lookup::Found) {
            // Not to whoever the message is already addressed to: a copy is
            // made where either the name or the address differs, and a second
            // netmail to the very recipient of the first is not a copy of
            // anything, it is the same message twice.
            if (!addressesTheSame(state.compose, found.copy)) {
                run.carbons.push_back(found.copy);
            }
        } else {
            keepLine(run, command.line);
            run.unresolved.push_back(token.text);
        }
        ++run.token;
    }
    finishCopies(state);
}

/// The message as a copy of it goes into `target`: the text as it now reads,
/// closed with that area's own tearline and origin, written from its own AKA
/// and in its own charset, and carrying a MSGID of its own.
///
/// `at` is which copy this is, and is what keeps the MSGIDs apart: the serial
/// number FTS-0009 asks for is the second the message was written in, so copies
/// written in the same second would otherwise be one message as far as the
/// network is concerned.
domain::MessageDraft copyDraft(AppState& state, const config::AppConfig& config,
                               const domain::AreaConfig& target,
                               const app::ComposeFields& fields,
                               const std::vector<std::string>& body, size_t at) {
    const app::BuildRequest source = buildRequest(state);
    // The original it answers travels with it, so a copy of a reply still
    // points at what was answered; the area it was read in does not, since
    // nothing was moved and @oecho has nothing to say about a copy.
    app::BuildRequest request{
        config, target, fields, source.original, source.originalBody, nullptr};
    request.now = source.now + static_cast<std::time_t>(at) + 1;
    request.utcOffsetMinutes = source.utcOffsetMinutes;
    request.kludgesShown = source.kludgesShown;
    request.extraKludges = state.copyRun->kludges;
    return app::buildDraft(request, body);
}

/// The fields a carbon copy is written with: a new message in the area it is
/// going into — the attributes a netmail of one's own starts with, the AKA that
/// area is presented under — addressed to the recipient the `CC:` line named
/// and carrying the subject of the message it copies.
app::ComposeFields carbonFields(AppState& state, const config::AppConfig& config,
                                const domain::AreaConfig& target,
                                const app::CarbonCopy& copy) {
    app::ComposeFields fields = app::newMessage(config, target);
    fields.subject = state.compose.subject;
    fields.toName = copy.name;
    fields.toAddr = copy.address.toString();
    // The same choice leaving the To address makes: the destination decides
    // which of our AKAs the message goes out under.
    if (const auto sender = app::senderFor(state.config, copy.address)) {
        fields.fromAddr = *sender;
    }
    return fields;
}

/// The fields a crosspost is written with: the header of the message itself —
/// the recipient, the subject and the attributes it was written with — under
/// the name and the AKA the echo it is going into is written under.
app::ComposeFields crosspostFields(AppState& state, const config::AppConfig& config,
                                   const domain::AreaConfig& target) {
    app::ComposeFields fields = state.compose;
    fields.netmail = target.hasAddressedRecipient();
    fields.moved = false;
    fields.direct = false;
    fields.forward = false;
    fields.changing = false;
    if (!config.userName.empty()) fields.fromName = config.userName;
    fields.fromAddr = app::ownAddress(config, target);
    // An echo is addressed to nobody in particular, and the address a netmail
    // header carried means nothing here.
    fields.toAddr.clear();
    if (fields.toName.empty()) fields.toName = "All";
    return fields;
}

/// Writes every copy the commands asked for.
///
/// One base is open at a time, so each target's takes the place of the one being
/// read and the reader's own is opened again at the end — the swap
/// `storeElsewhere()` makes, once per copy. Nothing on the screen underneath
/// comes off the base while it is away.
///
/// false is the reader's own area failing to open again, which leaves nothing
/// underneath to come back to.
bool writeCopies(AppState& state, const std::vector<std::string>& text) {
    const AppState::CopyRun& run = *state.copyRun;
    if (run.carbons.empty() && run.crossposts.empty()) return true;

    const domain::AreaConfig source = state.currentArea;
    // Every area closes the message with a pair of its own, so the one the
    // editor closed it with comes off first.
    const std::vector<std::string> body = app::withoutTrailer(text);

    const auto writeInto = [&state](const domain::AreaConfig& target,
                                    const domain::MessageDraft& draft) {
        const auto into = state.manager.openArea(target);
        if (!into) return;
        if ((*into)->write(draft)) state.manager.refreshArea(target);
    };

    // The base of the area being read is closed by opening another, so nothing
    // may be left pointing at it in between.
    state.base = nullptr;
    size_t at = 0;
    if (const auto target = carbonArea(state); target && !run.carbons.empty()) {
        const config::AppConfig config = state.config.effectiveFor(*target);
        for (const auto& copy : run.carbons) {
            writeInto(*target,
                      copyDraft(state, config, *target,
                                carbonFields(state, config, *target, copy), body, at++));
        }
    }
    for (const auto& post : run.crossposts) {
        const config::AppConfig config = state.config.effectiveFor(post.area);
        writeInto(post.area,
                  copyDraft(state, config, post.area,
                            crosspostFields(state, config, post.area), body, at++));
    }

    state.base = state.manager.openArea(source).value_or(nullptr);
    return state.base != nullptr;
}

/// What the copies could not find, said once the message is stored.
///
/// The lines naming those recipients and areas are still in the message — a
/// command nobody could carry out is not a reason to throw away what was
/// written — so the box says what was not done rather than what was lost. It
/// leaves the user where they are: the screen underneath is the reader on the
/// message that has just been written.
void reportUnresolved(AppState& state, const std::vector<std::string>& unresolved) {
    if (unresolved.empty()) return;

    std::string named;
    for (const auto& what : unresolved) {
        if (!named.empty()) named += ", ";
        named += what;
    }
    state.errorMessage = "No copy was made for: " + named +
                         ". The lines naming them are still in the message.";
    state.errorEndsScreen = false;
}

/// Asks whether to store the message — what Ctrl-S, F2 and the Save button all
/// do alike. A message missing an address it cannot be written without is not
/// asked about at all: the cursor goes up to the field at fault, which is the
/// answer storing it would have given a keystroke later.
void askToSave(AppState& state) {
    if (!addressesReady(state)) return;
    // Every attempt to store asks about the commands in the message afresh: the
    // text may have been typed into since the last one, and an answer given
    // about lines that have changed is no answer at all.
    state.copyRun.reset();
    state.confirm = AppState::Confirm::SaveMessage;
    state.confirmChoice = AppState::ConfirmChoice::Yes;
}

/// Puts the cursor where the pointer is: in the field of the header it landed
/// on, or on the line of the text it landed on, and in both cases at the
/// character under it. false is a click that landed on neither, which the screen
/// then goes on to test against everything else.
///
/// This is the whole of moving between the header and the text with the mouse —
/// there is no separate step for it, because a click that names a field is
/// already saying the typing goes there.
bool clickToCursor(AppState& state, const MouseEvent& click) {
    // The one thing leaving a field does beyond moving the cursor: off the To
    // address, the AKA the message is written from is chosen for whoever it is
    // now addressed to, and the From row above shows it. An address that does
    // not parse does not hold the pointer the way it holds Enter — the click has
    // already said where it wants to be, and what a message cannot be stored
    // without is checked where it is stored.
    const auto leavingFor = [&](int field) {
        if (state.composeInHeader && state.composeField == kToAddr && field != kToAddr) {
            leaveToAddr(state);
        }
    };

    for (size_t i = 0; i < state.composeFieldSpots.size(); ++i) {
        const AppState::ComposeSpot& spot = state.composeFieldSpots[i];
        if (!spot.box.Contain(click.x, click.y)) continue;

        const auto which = static_cast<int>(i);
        leavingFor(which);
        state.composeInHeader = true;
        state.composeField = which;
        state.composeCursor = offsetAtColumn(valueOf(state.compose, which), spot.origin,
                                             click.x - spot.box.x_min);
        return true;
    }

    for (size_t i = 0; i < state.composeTextRows.size(); ++i) {
        const AppState::ComposeSpot& spot = state.composeTextRows[i];
        if (!spot.box.Contain(click.x, click.y)) continue;
        if (state.edit.lines.empty()) return true;

        // What the row showed, read off the scroll and the layout the frame was
        // drawn with: leaving the header can expand the template again
        // underneath it, and what was pointed at is what was on screen. A row
        // is not a line now — a line too wide for the window takes several of
        // them — so which line it was is the layout's answer to give.
        const TextLayout layout = textLayout(state);
        const int index = std::clamp(state.editScroll + static_cast<int>(i), 0,
                                     static_cast<int>(layout.rows.size()) - 1);
        const EditRow row = layout.rows[static_cast<size_t>(index)];
        // Coming down out of the header is the same departure Enter off its last
        // field makes, template and all — a click is not a quieter way down.
        leavingFor(kFieldCount);
        if (state.composeInHeader) leaveHeader(state);

        state.edit.row =
            std::clamp(row.line, 0, static_cast<int>(state.edit.lines.size()) - 1);
        // Never past the row that was pointed at. A row that closes its line
        // ends where the typing goes on from; one the line goes on past ends on
        // its last character, the byte after it being drawn on the row below —
        // and a click lands where it points, not a row further down.
        const std::string& line = state.edit.line();
        size_t limit = std::min(row.end, line.size());
        if (row.end < line.size()) limit = prevChar(line, limit);
        state.edit.col =
            std::min(offsetAtColumn(line, spot.origin, click.x - spot.box.x_min), limit);
        scrollToCursor(state);
        return true;
    }
    return false;
}

/// The reply itself, into the area the message is being read in: every field
/// off the message it answers, and the cursor in the text.
///
/// Apart from `startReply()`, which decides first whether the answer is going
/// anywhere else at all, so that the decision is made once and not again by the
/// area it lands in.
void replyHere(AppState& state) {
    state.compose = app::reply(state.areaConfig, state.currentArea, state.currentArea,
                               *state.readHeader);
    // Straight into the text: a reply has every field filled in from the message
    // it answers, and what the user came here to do is answer it. The cursor is
    // left on the subject for when Alt-H brings it back up — that is the field
    // worth a second look, carried over unchanged, and a thread that has
    // wandered gets renamed there.
    moveTo(state, kSubject);
    openCompose(state, /*inHeader=*/false);
}

/// The reply into an area other than the one it was read in — what both ways of
/// asking for one end at, `direct` saying which asked.
///
/// The user's own `n` and the message's own `AREA:` line differ in one thing:
/// what the network is told about it. A reply the user has moved is answering a
/// message posted somewhere else, and the template's @moved lines say so; one
/// following the message's own line is answering it in the echo it says it came
/// from, and there is nothing to say — see `ComposeFields::direct`.
void replyInto(AppState& state, const domain::AreaConfig& target, bool direct) {
    state.targetArea = target;
    // Prefilled against the area it is going into rather than the one it
    // answers: the sender's name and AKA are that area's, and whether the
    // message is addressed to a node at all is decided there — an echo answered
    // into netmail is written as netmail, to whoever wrote it, and from the AKA
    // `akamatch` gives for them. The area it was read in goes in as well: what
    // the message was written to means something in netmail and nothing in an
    // echo. The settings are worked out for the target here rather than through
    // composeConfig(), which cannot answer yet: what makes composeArea() the
    // target is the `moved` flag two lines below, and app::reply() returns
    // fields without it.
    state.compose = app::reply(state.config.effectiveFor(state.targetArea),
                               state.currentArea, state.targetArea, *state.readHeader);
    state.compose.moved = true;
    state.compose.direct = direct;
    moveTo(state, kSubject);
    openCompose(state, /*inHeader=*/false);
}

/// The area the message on screen asks its answers to be posted to, or nothing
/// where it asks for none — which is the usual answer, and the one every branch
/// below falls back to.
///
/// A message that came in a packet begins with `AREA:` and the echo it was
/// posted to. Where the base has kept that line, it says where the answer
/// belongs, and the area it is being read in need not be that echo at all — a
/// dupe collector, a bad-message area, a carbon copy. `areareplydirect` is what
/// turns following it on and off, and it is read off the settings of the area
/// being read: the question is about this base's messages, so a group covering
/// it is what answers.
///
/// The tag has to name an area the tosser config declares, since a message is
/// written into a base and not into a name; one naming the area it is already
/// in is no move at all, and answering it here rather than in `startReplyTo()`
/// is what keeps the two from calling each other round in a circle.
const domain::AreaConfig* directReplyArea(const AppState& state) {
    if (!state.areaConfig.areaReplyDirect || !state.readBody) return nullptr;

    const std::string tag = app::areaTagOf(*state.readBody);
    if (tag.empty() || config::text::iequals(tag, state.currentArea.tag)) return nullptr;

    for (const auto& entry : state.manager.areas()) {
        if (config::text::iequals(entry.config.tag, tag)) return &entry.config;
    }
    // An echo that is named but not subscribed, or one renamed since the
    // message arrived. There is nowhere to put the answer but here.
    return nullptr;
}

}  // namespace

void openMenu(AppState& state) {
    std::vector<AppState::MenuView::Item> items;
    items.reserve(state.config.composeMenu.size());
    // Both of the editor's commands are about the message rather than about the
    // line the cursor is on, so neither goes quiet while the typing is up in the
    // header: a file read in from there goes into the text, which is the only
    // place a file could go, and the typing follows it down.
    for (const config::MenuCommand command : state.config.composeMenu) {
        items.push_back({command, true, {}});
    }
    menu_dialog::open(state, std::move(items));
}

void runMenuCommand(AppState& state, config::MenuCommand command) {
    switch (command) {
        // Save asks the same question Ctrl-S asks rather than storing the
        // message outright: a click is no surer than a key.
        case config::MenuCommand::Save: askToSave(state); break;
        case config::MenuCommand::Import: import_dialog::open(state); break;
        // The reader's own, which `compose_menu` cannot name.
        default: break;
    }
}

void startNew(AppState& state) {
    state.compose = app::newMessage(state.areaConfig, state.currentArea);
    // Who the message is for is the one thing a new one cannot be written
    // without, and the only field prefill leaves empty; the name and address
    // above it are the ones the area decided.
    moveTo(state, kToName);
    openCompose(state, /*inHeader=*/true);
}

void startReply(AppState& state) {
    if (!state.readHeader) return;

    // Where the message says which echo it belongs to, the answer follows it —
    // the same reply into another area the dialog's `n` starts, begun without
    // asking, since the message has already said where. Copied out of the
    // manager's list rather than pointed at, as the dialog's own answer is.
    if (const domain::AreaConfig* found = directReplyArea(state)) {
        const domain::AreaConfig target = *found;
        replyInto(state, target, /*direct=*/true);
        return;
    }
    replyHere(state);
}

void startReplyTo(AppState& state, const domain::AreaConfig& target) {
    if (!state.readHeader) return;

    // The area the message is already in is a reply and not a move, however it
    // was asked for: the base would be swapped for itself, and the template
    // would write "Answering a msg posted in area X" in area X. Tag and path
    // together are what name an area here, as they do everywhere else.
    //
    // The plain reply rather than startReply(): an area asked for by name is
    // asked for, and an `AREA:` line saying otherwise does not get to overrule
    // the user who has just picked this one out of the dialog.
    if (target.tag == state.currentArea.tag && target.path == state.currentArea.path) {
        replyHere(state);
        return;
    }
    replyInto(state, target, /*direct=*/false);
}

void startForwardTo(AppState& state, const domain::AreaConfig& target) {
    if (!state.readHeader) return;

    state.targetArea = target;
    // A new message of ours in the area it is going to — addressed to that area
    // rather than to whoever wrote what is being passed on, and written from
    // that area's AKA. What the message being forwarded gives it is its subject,
    // which is what the message is about wherever it is read, and its text,
    // which the template puts in where @message stands.
    // Under the target area's settings, and worked out here for the reason
    // startReplyTo() gives: `forward` is what makes it the compose area, and it
    // is set on the line below.
    state.compose =
        app::newMessage(state.config.effectiveFor(state.targetArea), state.targetArea);
    state.compose.forward = true;
    state.compose.subject = state.readHeader->subject;
    // On the recipient, as a new message opens: an echo has "All" in it already
    // and netmail has nobody yet, and either way that is the field to look at
    // first. The subject below it is filled in and rarely wants changing.
    moveTo(state, kToName);
    openCompose(state, /*inHeader=*/true);
}

void startChange(AppState& state, bool notice) {
    if (!state.readHeader || !state.readBody) return;

    state.compose = app::change(state.currentArea, *state.readHeader);
    state.changeNumber = state.readHeader->number;
    // What the base holds and the editor does not show: the control lines
    // either side of the text, and the charset the message is written in. They
    // go back around it when it is stored — the editor is for the words.
    state.changeKept = app::preservedLines(*state.readBody);

    // The message itself, as it is read: the service lines are the ones taken
    // off above, and the tearline and the origin stay — they are lines of the
    // message like any other here, and whoever is changing it may want them.
    std::vector<std::string> lines;
    if (notice) {
        // "*** Changed by ..." at the head of the message, from the template.
        // It is added to the text rather than written into the base afterwards:
        // it is part of the message from here on, and can be edited or deleted
        // like the rest of it before it is stored.
        lines = app::changeNotice(buildRequest(state));
    }
    for (const auto& line : state.readBody->lines) {
        if (!line.kludge) lines.push_back(line.text);
    }

    // On the subject, for when Alt-H brings the typing up into the header: it
    // is the field a changed message most often wants changed with it, and the
    // typing starts in the text.
    moveTo(state, kSubject);
    openWithText(state, std::move(lines));
}

namespace {

/// Whether the field holds nothing but blank, which is what "not filled in"
/// means for a row somebody may have tabbed through.
bool isBlank(const std::string& value) {
    return value.find_first_not_of(" \t") == std::string::npos;
}

/// Expands an address macro standing where the recipient's name goes, and
/// answers whether it did — what `address_macro` is for: `af` typed into the To
/// name row and Enter, and the whole of the robot's row is filled in, password
/// and attributes with it.
///
/// Only in netmail, which is the only place there is an address to fill in.
/// The macro decides the name, the address, and — where the line named them —
/// the subject and the attributes; the attributes are **added** to what the
/// message carries, `Loc` and `Pvt` being what a netmail of one's own starts
/// with and nothing a macro means to take away.
///
/// The whole field has to be the macro, so an ordinary name falls through to the
/// nodelist below and a name a macro happens to be spelled inside of is left
/// alone. Whatever stood in the address row is written over: the macro names a
/// recipient outright, and half of one from somewhere else is not that
/// recipient — the same reasoning `useNode()` fills both halves in by.
bool applyAddressMacro(AppState& state) {
    if (!state.compose.netmail || state.composeField != kToName) return false;

    // The file's own config rather than the area's: a macro is not a setting an
    // area group may state, and the whole of what it addresses is a node.
    const config::AddressMacro* macro =
        state.config.addressMacroFor(state.compose.toName);
    if (macro == nullptr) return false;

    state.compose.toName = macro->name;
    state.compose.toAddr = macro->address.toString();
    if (macro->subject) state.compose.subject = *macro->subject;
    if (macro->attributes) state.compose.attributes |= *macro->attributes;

    // The same thing leaving the address field by hand does: the destination
    // decides which of our AKAs the message goes out under, and it has just
    // been answered.
    leaveToAddr(state);

    // On the subject, as a node picked out of the nodelist leaves the cursor:
    // the To row is whole, and there is nothing left on it to stand on. A macro
    // that filled the subject in as well leaves the cursor at the end of it,
    // where it can be typed over or added to.
    moveTo(state, kSubject);
    return true;
}

/// Opens the nodelist on half a filled-in To row, and answers whether it did.
///
/// A netmail is addressed by a name and a number, and one of the two is nearly
/// always the only one to hand: the address of somebody whose name is known, or
/// the name of whoever is at an address off a message header. Enter on the half
/// that is filled in, with the other half empty, is what asks — which is a key
/// that would otherwise have walked to the very field being asked about.
///
/// Only in netmail, and only with exactly one of the two filled in: an echomail
/// header addresses nobody in particular, and a row that is already whole is a
/// row with nothing to look up.
bool askNodelist(AppState& state) {
    if (!state.compose.netmail) return false;

    if (state.composeField == kToName && !isBlank(state.compose.toName) &&
        isBlank(state.compose.toAddr)) {
        nodelist_dialog::openFor(state, AppState::NodelistView::Purpose::PickAddress,
                                 state.compose.toName);
        return true;
    }
    if (state.composeField == kToAddr && !isBlank(state.compose.toAddr) &&
        isBlank(state.compose.toName)) {
        nodelist_dialog::openFor(state, AppState::NodelistView::Purpose::PickName,
                                 state.compose.toAddr);
        return true;
    }
    return false;
}

/// A keystroke aimed at the header block. false is a key the block does not
/// bind, which the screen around it then gets a look at.
bool headerKey(AppState& state, const Event& event) {
    // The attributes are a button and not a field, so what they answer to is
    // what a button answers to. Only the walking below is shared with the
    // fields; the editing under it would work on text the button does not
    // have.
    const bool onButton = state.composeField == kAttributes;
    if (onButton && (event == Event::Return || event == Event::Character(' '))) {
        attributes_dialog::open(state);
        return true;
    }

    // Enter on a To name that is a macro fills the whole row in from the config,
    // and Enter on half a To row asks the nodelist for the other half — see
    // `applyAddressMacro()` and `askNodelist()`. Both come before the walking
    // below, since that is what Enter would otherwise do, and the macro comes
    // first: a word the config gives an address to is not a name to go looking
    // for in the nodelist.
    if (event == Event::Return && (applyAddressMacro(state) || askNodelist(state))) {
        return true;
    }

    if (event == Event::Return || event == Event::Tab || event == Event::ArrowDown) {
        if (state.composeField == kToAddr && !leaveToAddr(state)) return true;

        // Enter walks the fields that are typed into and no further: off the
        // subject it hands the typing down to the message, which is what the
        // user came to write, rather than stopping at a button on the way. Tab
        // and ↓ walk the whole ring, the button among the rest.
        const int last = event == Event::Return ? kAttributes : kFieldCount;
        const int next = fieldAfter(state, 1);
        if (next >= last) {
            // Off the last stop is down into the text — the addresses are not
            // checked here. An address is typed through states that do not
            // parse, and the header is no longer a gate the message has to pass
            // through: what needs one is storing it, and saveMessage() is where
            // the cursor is sent back up to the field at fault.
            leaveHeader(state);
            return true;
        }
        moveTo(state, next);
        return true;
    }
    if (event == Event::TabReverse || event == Event::ArrowUp) {
        // Leaving the To address is what picks the AKA to write from, whichever
        // way the cursor is going. Backwards it does not hold the cursor the way
        // forwards does: what is behind the field is already filled in, and an
        // address still half typed is no reason to refuse to look at it.
        if (state.composeField == kToAddr) leaveToAddr(state);
        // Shift-Tab closes the ring Tab walks: off the first field is down into
        // the text, exactly as off the last field is going the other way. The
        // arrows are not the ring — ↓ off the last field goes on into the text
        // because the text is drawn below the block, and there is nothing above
        // the first field for ↑ to reach.
        if (event == Event::TabReverse && state.composeField == kFromName) {
            leaveHeader(state);
            return true;
        }
        moveTo(state, fieldAfter(state, -1));
        return true;
    }

    // Past the walking, and everything past it is about text. The button has
    // none: a key it does not bind is the screen's, not a no-op of its own.
    if (onButton) return false;

    std::string& value = valueOf(state.compose, state.composeField);
    size_t& cursor = state.composeCursor;

    if (event == Event::ArrowLeft) {
        cursor = prevChar(value, cursor);
        return true;
    }
    if (event == Event::ArrowRight) {
        cursor = std::min(value.size(), cursor + charLen(value, cursor));
        return true;
    }
    // The ends of the field, from the keys marked for them and from the chords
    // a terminal that sends nothing for Home and End leaves as the only way
    // there — the pair every readline-shaped line editor answers.
    if (event == Event::Home || state.keys.is(event, KeyCommand::ComposeLineStart)) {
        cursor = 0;
        return true;
    }
    if (event == Event::End || state.keys.is(event, KeyCommand::ComposeLineEnd)) {
        cursor = value.size();
        return true;
    }
    if (event == Event::Backspace) {
        const size_t from = prevChar(value, cursor);
        value.erase(from, cursor - from);
        cursor = from;
        return true;
    }
    if (event == Event::Delete) {
        value.erase(cursor, charLen(value, cursor));
        return true;
    }
    // Any chord is swallowed rather than typed: a chord arrives as a character
    // event with Ctrl or Alt held, and Ctrl-X falling through to be typed into
    // the subject as `x` is exactly what must not happen. The chords this screen
    // does bind were answered before the key got here.
    if (event.ctrl() || event.alt()) return false;
    // Whatever the terminal reports as text. The input layer decodes a code
    // point before making an event of it, so a Cyrillic letter arrives whole and
    // is inserted as one; control bytes are not text and are left to the
    // branches above.
    if (event.is_character()) {
        const std::string& input = event.character();
        if (input.size() == 1 && static_cast<unsigned char>(input[0]) < 0x20)
            return false;
        // A full field takes nothing more: what does not fit in the base would
        // be cut off there without a word, and a name half stored is worse than
        // one the editor would not let be typed. The keystroke is swallowed
        // rather than passed on — it was aimed at this field, and there is
        // nowhere else on the screen for it to go.
        const size_t limit = fieldLimit(state.composeField);
        if (limit != 0 && charCount(value) + charCount(input) > limit) return true;
        value.insert(cursor, input);
        cursor += input.size();
        return true;
    }
    return false;
}

/// A keystroke aimed at the text.
bool textKey(AppState& state, const Event& event) {
    // The margin of the area this message is going into, which is the same one
    // the quoting was done at: the editor rewraps a quote as it is typed, and
    // the two answering differently would show a line moving as it was edited.
    const EditOptions options{state.composeConfig().quoteMargin};

    // The layout first, as on every other screen: a key it has been given is
    // that command and not what the key would otherwise have typed or deleted.
    // Word motion has to come before the arrows for that reason alone — Alt-Left
    // is a left arrow with a modifier on it — and the rest follow the same rule
    // so that Del bound to a command is that command here too.
    if (state.keys.is(event, KeyCommand::ComposeDeleteLine)) {
        deleteLine(state.edit);
    } else if (state.keys.is(event, KeyCommand::ComposeRestoreLine)) {
        restoreLine(state.edit);
    } else if (state.keys.is(event, KeyCommand::ComposeDeleteQuote)) {
        deleteQuote(state.edit);
    } else if (state.keys.is(event, KeyCommand::ComposeDeleteWord)) {
        deleteWordBefore(state.edit);
    } else if (state.keys.is(event, KeyCommand::ComposeWordRight)) {
        moveWordRight(state.edit);
    } else if (state.keys.is(event, KeyCommand::ComposeWordLeft)) {
        moveWordLeft(state.edit);
    } else if (event == Event::Return) {
        insertNewline(state.edit);
    } else if (event == Event::Backspace) {
        deleteBefore(state.edit);
    } else if (event == Event::Delete) {
        deleteAt(state.edit);
    } else if (event == Event::ArrowLeft) {
        moveLeft(state.edit);
    } else if (event == Event::ArrowRight) {
        moveRight(state.edit);
    } else if (event == Event::ArrowUp) {
        moveRows(state, -1);
    } else if (event == Event::ArrowDown) {
        moveRows(state, 1);
    } else if (event == Event::PageUp) {
        moveRows(state, -std::max(1, editorRows(state)));
    } else if (event == Event::PageDown) {
        moveRows(state, std::max(1, editorRows(state)));
    } else if (event == Event::Home ||
               state.keys.is(event, KeyCommand::ComposeLineStart)) {
        moveToLineStart(state.edit);
    } else if (event == Event::End || state.keys.is(event, KeyCommand::ComposeLineEnd)) {
        moveToLineEnd(state.edit);
    } else if (event.ctrl() || event.alt()) {
        // A chord this layout does not bind is swallowed rather than typed, the
        // same as in the header block above. On a terminal reporting modified
        // keys a chord arrives as the letter with a flag on it, and Ctrl-K
        // falling through to put a `k` in the message is exactly what must not
        // happen.
        return false;
    } else if (event.is_character()) {
        const std::string& input = event.character();
        if (input.size() == 1 && static_cast<unsigned char>(input[0]) < 0x20)
            return false;
        insertText(state.edit, input, options);
    } else {
        return false;
    }

    scrollToCursor(state);
    return true;
}

}  // namespace

void editHeader(AppState& state) {
    // Onto the field the cursor last stood in, its own cursor at the end of
    // what is there — which is where typing carries on from.
    enterHeader(state, state.composeField);
}

bool addressesReady(AppState& state) {
    // A message being changed is not being addressed. Nothing is made from its
    // addresses — it carries the MSGID and the INTL it was written with — and
    // it is stored with whatever the base had: JAM keeps no sender address at
    // all in an echo area, and demanding one here would have the user invent
    // one for every message in such an area before it could be put back.
    if (state.compose.changing) return true;

    const int wrong = badAddressField(state);
    if (wrong < 0) return true;
    // The cursor going up to the field at fault is what says which it is: there
    // is no status line to say it in, and the block is on the screen already.
    enterHeader(state, wrong);
    return false;
}

int editorRows(const AppState& state) {
    // The title, a rule, the rows of the header block and the rule closing them
    // off. The menu button costs no row: it stands in the two the title and the
    // rule already take.
    const int chrome = 3 + state.headerRows();
    return state.height <= chrome ? 1 : state.height - chrome;
}

Element render(AppState& state) {
    // The header block, with the cursor in it only while the typing goes there.
    Elements content = headerRows(state, state.composeInHeader);
    // The block is closed off the way the reader closes its own, so that the
    // text below starts where the eye expects it to.
    content.push_back(text(horizontalRule(state.width)) |
                      color(theme::palette.separator));

    // The text, drawn as the reader draws a body — quotes in their own colors,
    // so that what is being answered stands apart from the answer, and the
    // tearline and origin in theirs. Which lines those are is left to the same
    // function the reader uses, so that a pair the typing has pushed out of
    // place stops being a trailer here exactly when it stops being one there.
    std::vector<domain::MessageLine> marked;
    marked.reserve(state.edit.lines.size());
    for (const auto& line : state.edit.lines) marked.push_back({line, false, false});
    domain::markTrailer(marked);

    const int rows = editorRows(state);
    // Where a line breaks depends on how wide the window is, so a window that
    // has been resized has a scroll worked out against rows that are no longer
    // there. Asking again here is what keeps the cursor on screen across a
    // resize; on a frame nothing has moved for it changes nothing.
    scrollToCursor(state);

    // The text as it falls into rows of the window. A line wider than the
    // window is shown over several of them and is still one line: the message
    // keeps what was typed into it, and a break the window put there is not a
    // carriage return the reader at the other end would ever see.
    const TextLayout layout = textLayout(state);
    const auto cursorRow =
        static_cast<int>(rowOfCursor(layout.rows, state.edit.row, state.edit.col));

    // The bar beside the text, where the message is longer than the window.
    // Drawn a cell at a time rather than as a column of its own: every row of
    // the text is a row of the frame, which is what the blank fill under the
    // message is counted against. The row itself
    // flexes into what is left, so a click still lands anywhere along it.
    const scrollbar::Thumb thumb =
        scrollbar::thumbOf(rows, static_cast<int>(layout.rows.size()), state.editScroll);

    // The delete-line button, standing beside the row the cursor is on. The box
    // closes a row either way, and only over rows of the message: over the first
    // row of it there is the rule closing the header block, and under the last
    // the blank the message has stopped at, and a side reaching into either
    // would be closing round something that is not the line it deletes. The
    // window is the other limit — the cursor's row is always on the screen, but
    // it may be the top or the bottom row of it.
    const bool deleteButton = state.composeDeleteLineShown();
    const int deleteAt = cursorRow - state.editScroll;
    const bool deleteTop = deleteButton && cursorRow > 0 && deleteAt > 0;
    const bool deleteBottom = deleteButton &&
                              cursorRow + 1 < static_cast<int>(layout.rows.size()) &&
                              deleteAt + 1 < rows;
    const bool pressedDelete = state.isPressed(AppState::Pressed::DeleteLine);
    // Where it lands is decided by the frame, as every other button's is, and a
    // frame that draws no top or bottom leaves that box where nothing can be
    // clicked.
    state.composeDeleteLine = AppState::DeleteLineSpots{};

    // What stands in the three reserved columns of one row: the button where it
    // has reached this row, and otherwise the bar — in the last of them, the one
    // the box's right-hand side stands in, with the rest left blank.
    const auto rightOf = [&](int at) -> Element {
        if (deleteButton) {
            if (at == deleteAt) {
                return delete_line_button::labelRow(pressedDelete) |
                       reflect(state.composeDeleteLine.label);
            }
            if (deleteTop && at == deleteAt - 1) {
                return delete_line_button::topRow(pressedDelete) |
                       reflect(state.composeDeleteLine.top);
            }
            if (deleteBottom && at == deleteAt + 1) {
                return delete_line_button::bottomRow(pressedDelete) |
                       reflect(state.composeDeleteLine.bottom);
            }
        }
        Element under = layout.scrollbar ? scrollbar::cell(at, thumb) : text(" ");
        if (!deleteButton) return under;
        // Counted off the button's own width rather than written out, so the two
        // cannot drift apart and leave the bar standing under the box's side.
        const auto blank = static_cast<size_t>(delete_line_button::kWidth - 1);
        return hbox({text(std::string(blank, ' ')), std::move(under)});
    };

    const auto placed = [&](Element row, int at) {
        if (!deleteButton && !layout.scrollbar) return row;
        return hbox({std::move(row) | flex, rightOf(at)});
    };

    // One per row on screen, the blank ones under the end of the message
    // included: a click there is still a click in the text.
    state.composeTextRows.assign(static_cast<size_t>(std::max(0, rows)),
                                 AppState::ComposeSpot{});
    for (int i = 0; i < rows; ++i) {
        auto& spot = state.composeTextRows[static_cast<size_t>(i)];
        const int index = state.editScroll + i;
        if (index < 0 || index >= static_cast<int>(layout.rows.size())) {
            content.push_back(placed(text("") | reflect(spot.box), i));
            continue;
        }
        const EditRow& row = layout.rows[static_cast<size_t>(index)];
        const std::string& line = state.edit.lines[static_cast<size_t>(row.line)];
        const std::string piece = line.substr(row.begin, row.end - row.begin);
        // Read off the whole line rather than off the piece on this row: a
        // continuation carries none of the markers and is still the same quote,
        // as it is in the reader.
        const int depth = quoteDepth(line);
        const theme::Color base =
            marked[static_cast<size_t>(row.line)].trailer ? theme::palette.trailer
            : depth > 0
                ? (depth % 2 == 1 ? theme::palette.quoteOdd : theme::palette.quoteEven)
                : theme::palette.text;
        // Only one cursor on the screen: the text keeps it while the typing goes
        // there, and gives it up to the header block while that is being filled
        // in. Two would say the typing goes to both places.
        if (index != cursorRow || state.composeInHeader) {
            // Where the row begins in the line, for a click to be answered
            // against — the same thing `field()` works out for the row that
            // carries the cursor, which is the only one drawn scrolled.
            spot.origin = row.begin;
            content.push_back(placed(text(piece) | color(base) | reflect(spot.box), i));
            continue;
        }
        // The row the cursor is on is drawn like any other. The cursor says
        // where the typing goes, and a second mark saying the same thing would
        // only take the colors away from what they are for here: whether the
        // line is a quote, and how deeply.
        const size_t within = std::min(
            state.edit.col > row.begin ? state.edit.col - row.begin : 0, piece.size());
        content.push_back(placed(
            field(piece, within, layout.width, true, base, std::nullopt, &spot), i));
        // `field()` counts the scroll from the start of what it was given, and
        // what it was given starts partway into the line.
        spot.origin += row.begin;
    }

    return chrome(state, std::move(content));
}

bool handleEvent(AppState& state, const Event& event) {
    // The menu button in the top-right corner, before anything else a click
    // could mean: it stands over the header block's own first row.
    if (state.composeMenuShown() && menu_button::clicked(event, state.width)) {
        state.showClick(AppState::Pressed::MenuButton);
        openMenu(state);
        return true;
    }
    // The attributes, which are the button that sets them. Pointing at a stop of the
    // header is what puts the typing on it, the same as pointing at a field —
    // and this one, being a button, opens its dialog into the bargain. The
    // press is shown before the focus moves, so the animation is on the button
    // as it was drawn rather than on one that has just lit up.
    if (const auto click = leftClick(event);
        click && state.changeAttributesBox.Contain(click->x, click->y)) {
        state.showClick(AppState::Pressed::ChangeAttributes);
        enterHeader(state, kAttributes);
        attributes_dialog::open(state);
        return true;
    }

    // The delete-line button, which stands over the text and is asked before a
    // click in the text is: it is drawn in columns the text was laid out
    // without, so a click on it is never one the message could answer. Only the
    // press acts, as everywhere else — the release would arrive with the line
    // already gone and land on whatever had moved up into its place.
    if (const auto click = leftClick(event);
        click && state.composeDeleteLine.contains(click->x, click->y)) {
        state.showClick(AppState::Pressed::DeleteLine);
        deleteLine(state.edit);
        scrollToCursor(state);
        return true;
    }

    // Leaving and saving both ask first: a message is work, and either answer
    // to the wrong keystroke would throw it away or send it half-written. Both
    // are asked wherever the cursor is — the header is part of the message now,
    // not a screen in front of it.
    if (state.keys.is(event, KeyCommand::ComposeSave)) {
        askToSave(state);
        return true;
    }
    if (event == Event::Escape ||
        (state.backButtonShown() && back_button::clicked(event))) {
        // When it was the button, the press is shown on it before the dialog
        // comes up over it — Esc has nothing to show.
        if (back_button::clicked(event)) state.showClick(AppState::Pressed::Back);
        state.confirm = AppState::Confirm::DropMessage;
        state.confirmChoice = AppState::ConfirmChoice::Yes;
        return true;
    }
    // Pointing at the message itself, once the buttons standing over it have
    // had their look: the header block and the text are one screen, and a click
    // in either says the typing goes there, at the character pointed at.
    if (const auto click = leftClick(event); click && clickToCursor(state, *click)) {
        return true;
    }
    // The wheel over the message, a row per notch, exactly as it moves the body
    // in the reader — the cursor coming along where the text would otherwise
    // have scrolled out from under it. Answered wherever the typing is: the
    // message is on the screen while the header is being filled in too, and
    // that is reason enough to want to look further down it.
    if (const int wheel = wheelDelta(event); wheel != 0) {
        wheelScroll(state, wheel);
        return true;
    }
    // The attributes the message goes out with. The row under the addresses says
    // which it carries; setting them is the dialog's, which is what both the
    // button and this chord open.
    if (state.keys.is(event, KeyCommand::ComposeAttributes)) {
        attributes_dialog::open(state);
        return true;
    }
    // A file into the message, from anywhere on the screen — the header
    // included, since what is read goes into the text either way and the typing
    // follows it there.
    if (state.keys.is(event, KeyCommand::ComposeImport)) {
        import_dialog::open(state);
        return true;
    }
    // Back up into the header, asked only from the text — in the header Tab is
    // the next field.
    //
    // Tab and Shift-Tab close the ring the header's own Tab walks: the text
    // stands after the last field and before the first, so coming up out of it
    // lands on the first field going forwards and on the last going back. Round
    // and round either way, the whole header and the text in one cycle.
    //
    // Alt-H is the other way up, onto the field the cursor was last in — the
    // answer to "put me back where I was" rather than to "next". A chord of its
    // own rather than Ctrl-I: that one is the byte Tab has sent since ASCII, and
    // no terminal short of one reporting modified keys could tell the two apart.
    if (!state.composeInHeader) {
        if (state.keys.is(event, KeyCommand::ComposeHeaderBack)) {
            editHeader(state);
            return true;
        }
        if (event == Event::Tab) {
            enterHeader(state, kFromName);
            return true;
        }
        if (event == Event::TabReverse) {
            enterHeader(state, kAttributes);
            return true;
        }
    }

    return state.composeInHeader ? headerKey(state, event) : textKey(state, event);
}

void saveMessage(AppState& state) {
    if (state.base == nullptr) return;
    // The addresses a message cannot be written without. Checked here rather
    // than only where saving is asked for, this being the one door the message
    // leaves by; a failed check leaves the cursor on the field at fault.
    if (!addressesReady(state)) return;

    if (state.compose.changing) {
        // Over the message it came from, and nowhere else: no tearline is added
        // and nothing of the template, since this message was written once
        // already and is only being written again. The base keeps its number
        // and its place in the thread; what says the writing is ours and is
        // this moment's — the stamp, the zone and the new MSGID — is the stamp
        // below. A base that refuses it keeps the editor open on it, as
        // everywhere else here.
        const app::BuildRequest request = buildRequest(state);
        const app::ChangeStamp stamp{
            request.now, request.utcOffsetMinutes,
            app::ownAddress(state.composeConfig(), state.currentArea)};
        const auto draft = app::buildChange(state.compose, state.changeKept,
                                            trimmedLines(state.edit), stamp);
        if (!state.base->replace(state.changeNumber, draft)) return;

        // The counts can have moved with it — a message marked read or unread
        // is one more or one fewer unread in the area list.
        state.manager.refreshArea(state.currentArea);
        state.headers.clear();
        state.headersStart = 0;

        const uint32_t number = state.changeNumber;
        leaveEditor(state);
        message_read::loadMessage(state, number);
        return;
    }

    // The `CC:` and `XC:` lines the message carries are asked about before
    // anything is stored, and once per attempt to store: `processCopies()` and
    // `ignoreCopies()` are the two answers, and both come back here.
    if (!state.copyRun) {
        std::vector<app::CopyCommand> commands =
            commandsIn(state, trimmedLines(state.edit));
        if (!commands.empty()) {
            AppState::CopyRun run;
            run.commands = std::move(commands);
            state.copyRun = std::move(run);
            state.confirm = AppState::Confirm::ProcessCopies;
            state.confirmChoice = AppState::ConfirmChoice::Yes;
            return;
        }
    }

    // The message as it now reads: with the lists where its commands stood
    // where they were carried out, and exactly as it was typed where they were
    // not.
    const bool copying = state.copyRun && state.copyRun->process;
    const std::vector<std::string> text =
        copying ? state.copyRun->text : trimmedLines(state.edit);
    // What the copies could not find, taken before the run goes with the
    // message: the box saying so comes up once it has been stored.
    const std::vector<std::string> unresolved =
        copying ? state.copyRun->unresolved : std::vector<std::string>{};

    app::BuildRequest request = buildRequest(state);
    if (copying) request.extraKludges = state.copyRun->kludges;
    const auto draft = app::buildDraft(request, text);

    if (state.composeGoesElsewhere()) {
        // A base that would not take it keeps the editor open on the message,
        // which is the one thing that must not be lost here.
        if (!storeElsewhere(state, draft)) return;
        // The copies follow the message itself, so that a base refusing the
        // message is a message nothing was copied on account of. `false` is the
        // area on screen failing to open again, which storeElsewhere() has
        // already answered for where it happened there.
        if (copying && state.base != nullptr && !writeCopies(state, text)) {
            message_list::leaveArea(state);
        }
        // Nothing to open the reader on: it is already showing the message that
        // was answered or passed on, in the area it was read in. When that area
        // is the one that would not open again, leaveArea() has reset the
        // navigator and the pop below falls away — the area list has no screen
        // under it to go back to.
        leaveEditor(state);
        reportUnresolved(state, unresolved);
        return;
    }

    // A base that would not take it keeps the editor open on the message, which
    // is the one thing that must not be lost here.
    const auto written = state.base->write(draft);
    if (!written) return;
    const uint32_t number = *written;

    // And then the copies of it, into whatever areas they are for.
    if (copying && !writeCopies(state, text)) {
        // The area the message was written into will not open again. It is
        // stored all the same; there is simply nothing left underneath to show
        // it in.
        message_list::leaveArea(state);
        leaveEditor(state);
        reportUnresolved(state, unresolved);
        return;
    }

    // The area is one message longer, and the reader opens on the new one —
    // which is also what unblocks the message list in an area that was empty.
    // The area list is told to count again rather than given the one more it
    // could work out for itself: the base is what knows, and it is open.
    state.manager.refreshArea(state.currentArea);
    state.messageCount = state.base->count();
    state.headers.clear();
    state.headersStart = 0;
    state.messageCursor = static_cast<int>(number) - 1;

    leaveEditor(state);
    message_read::loadMessage(state, number);
    reportUnresolved(state, unresolved);
}

void processCopies(AppState& state) {
    if (!state.copyRun) return;
    state.copyRun->process = true;
    resolveCopies(state);
}

void ignoreCopies(AppState& state) {
    if (!state.copyRun) return;
    // The lines stay in the message as the text they were typed as, and nothing
    // is copied anywhere.
    state.copyRun->process = false;
    saveMessage(state);
}

void useCarbonCopy(AppState& state, const nodelist::NodeEntry* node) {
    if (!state.copyRun) return;
    AppState::CopyRun& run = *state.copyRun;
    if (run.command < run.commands.size()) {
        const app::CopyCommand& command = run.commands[run.command];
        if (run.token < command.tokens.size()) {
            const app::CopyToken& token = command.tokens[run.token];
            if (node != nullptr) {
                // The name as the nodelist spells it, which is the spelling the
                // system at the other end matches on.
                run.carbons.push_back(
                    app::CarbonCopy{node->sysop, node->address, token.hidden});
            } else {
                keepLine(run, command.line);
                run.unresolved.push_back(token.text);
            }
            ++run.token;
        }
    }
    resolveCopies(state);
}

void dropMessage(AppState& state) {
    leaveEditor(state);
}

void insertImported(AppState& state, const std::vector<std::string>& lines) {
    if (lines.empty()) return;
    // The typing may still be up in the header, Ctrl-O and the Import button
    // both being answered from either half of the screen. What was read goes
    // into the text, so the cursor goes there first — and that is the ordinary
    // way down, template and all, rather than a quieter one.
    if (state.composeInHeader) leaveHeader(state);

    // At the cursor where it stands at the start of a line, and after that line
    // where it stands anywhere else: a file is a block of whole lines, and the
    // line being written is left as it was written rather than cut in two round
    // it.
    const auto at = static_cast<size_t>(state.edit.row) + (state.edit.col == 0 ? 0 : 1);
    state.edit.lines.insert(state.edit.lines.begin() + static_cast<ptrdiff_t>(at),
                            lines.begin(), lines.end());

    // Under the block, which is where the writing goes on from. A block that
    // ends the message has no line under it to stand on, and the cursor stays
    // at the end of its last one.
    const auto after = at + lines.size();
    state.edit.row = static_cast<int>(std::min(after, state.edit.lines.size() - 1));
    state.edit.col = after < state.edit.lines.size() ? 0 : state.edit.line().size();
    scrollToCursor(state);
}

void useNode(AppState& state, AppState::NodelistView::Purpose purpose,
             const nodelist::NodeEntry& node) {
    // Browsing picks nothing, and there is nothing here for it to fill in.
    if (purpose == AppState::NodelistView::Purpose::Browse) return;

    // Both halves, from the node, whichever half was asked about: the row is
    // addressed to the node that was picked, and half a row from the nodelist
    // beside half a row from a search field is not that node. The name goes in
    // as the nodelist spells it — `Schroeter` was how the node was found, and
    // `Ulrich Schroeter` is who is there.
    state.compose.toName = node.sysop;
    state.compose.toAddr = node.address.toString();
    // The same thing leaving the address field by hand does: the destination
    // decides which of our AKAs the message goes out under, and it has just
    // been answered.
    leaveToAddr(state);

    // On the subject, which is the next thing to fill in: the To row is whole,
    // and leaving the cursor on either half of it would be asking the user to
    // walk past what they have just finished.
    moveTo(state, kSubject);
}

}  // namespace amberedit::ui::screens::compose
