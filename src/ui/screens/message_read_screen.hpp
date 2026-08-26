#pragma once

#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

#include <cstdint>
#include <string>

#include "app/message_search.hpp"
#include "ui/app_state.hpp"

/// The message reader: a header block (From/To/Subject/Date/addresses) and
/// the body, already converted to UTF-8.
namespace amberedit::ui::screens::message_read {

/// Draws the reader. Not const: where the thread markers beside the message
/// number land is written back as they are laid out, so that a click on one
/// is tested against what was drawn.
term::Element render(AppState& state);
bool handleEvent(AppState& state, const term::Event& event);

/// Puts the reader's context menu up — what the button in the top-right corner
/// does. What it holds is `reader_menu`, and whether each command can be run is
/// decided here and now, on the message in front of the user: one that cannot is
/// in the menu still, drawn quietly, since what it says is what it would do.
void openMenu(AppState& state);

/// Runs one of those commands, which is what the shell does once the menu has
/// been answered. Every one of them is a thing a key on this screen does as
/// well; the menu says what the reader offers rather than offering anything
/// else.
void runMenuCommand(AppState& state, Command command);

/// Loads message msgNumber (1-based) into the reader state.
///
/// Whatever the last message left behind goes first, and unconditionally: the
/// header and body, the laid-out lines, where the text was scrolled to, a twit
/// shown after all, what a search lit, a number half typed. False means there
/// was no such message to load — the base is not open, or the number is past
/// the end of the area — and the reader is then left showing nothing rather
/// than still showing the message before it.
bool loadMessage(AppState& state, uint32_t msgNumber);

/// Opens the area *at* msgNumber: that message, or the nearest one to it the
/// `twit_mode` in force does not walk past — forwards from there, which is the
/// direction reading runs in, and backwards from it when everything after it is
/// a twit. What entering an area does and what picking a message out of the list
/// does, both of them naming a place in the area rather than one message in
/// particular.
///
/// Where every message in the area is a twit there is nowhere to go, and the one
/// asked for is opened with its text behind the notice — `blank`'s behaviour,
/// which is what `skip` falls back to. The list's cursor follows either way.
void openMessage(AppState& state, uint32_t msgNumber);

/// Puts the reader on no message at all — blank header rows, no body, no
/// thread markers — which is what an area holding nothing shows. Everything
/// the message before it left behind is dropped, so that nothing of the last
/// area read is still on screen beside an empty one's title.
void showEmptyArea(AppState& state);

/// Re-wraps the body if the terminal width changed.
void relayout(AppState& state);

/// Opens message `number` in the reader, keeping the message list's cursor on
/// it — what the thread keys and a click on a thread marker do.
void goToMessage(AppState& state, uint32_t number);

/// Looks for `query` from the message on screen to the end of the area, and
/// opens the first one that holds it — scrolled to the occurrence, with every
/// occurrence in it lit. false means nothing after where the reader stands has
/// it, and the reader is left exactly as it was.
///
/// **The same search made again starts on the message after the one it found**,
/// so answering the Find dialog twice walks from occurrence to occurrence. That
/// only holds where the reader is still standing on what the last search landed
/// on, and in the same area: `AppState::LastFind` is the whole of that memory.
///
/// The messages `twit_mode` walks past are walked past here too — `ignore`
/// passes over every twit and `skip` over the ones not addressed to the user.
/// One found under `blank` or `kill` opens behind the usual notice: whose
/// message is being passed over is what the user is entitled to see, and Space
/// shows the text with the occurrences lit in it.
///
/// The matching is folded by the charset the message declares rather than by the
/// locale — see `encoding::TextSearch`.
bool findMessage(AppState& state, const std::string& query, app::SearchScope scope);

/// Takes the message on screen out of the base and shows what follows it —
/// the answer to the delete confirmation. The one before it where it was the
/// last, and blank rows where it was the only one.
void deleteMessage(AppState& state);

/// Puts up the dialog asking which area a message is to go into — what `n` asks
/// outright, and what the Forward dialog's three answers all lead to. Does
/// nothing where there is no message to write about, or no area to write into.
///
/// The cursor opens on the first area of the list, which is the top of the same
/// list the area list screen shows: the area being read is the one place the
/// message is usually not going, `q` and `e` writing here already. A reply
/// opens on the area `reply_to_area` names instead, where the config names one
/// the list holds.
void askArea(AppState& state, AppState::AreaPicker::For purpose);

/// Puts the message on screen into `target` as it stands — the same message in
/// two areas, which is what Copy asks for. The reader does not move: what it is
/// showing is still there.
///
/// `target` may be the area being read, and then the message is written into the
/// base already open — a second copy of it beside the first, which is what
/// copying a message into the area it is in can only mean.
void copyMessage(AppState& state, const domain::AreaConfig& target);

/// The same, and then the message is taken out of the area it was read in —
/// Move. The reader lands on what followed it, exactly as a delete leaves it.
///
/// Nothing is deleted until the message is safely in the other area: a write
/// that failed would otherwise cost the message both places. `target` naming the
/// area being read does nothing at all — the message is already there.
void moveMessage(AppState& state, const domain::AreaConfig& target);

/// The words down the left of the header block, and the column they stand in.
///
/// Here rather than in either screen because both draw the block and the two
/// have to line up field for field — the reader and the editor show the same
/// message. A translation is what makes the width worth asking for: `From` is
/// four columns and its Russian is two, and a label longer than the column would
/// push the name column sideways on one screen and not the other.
namespace header_labels {

[[nodiscard]] const char* from();
[[nodiscard]] const char* to();
[[nodiscard]] const char* subject();
[[nodiscard]] const char* date();
[[nodiscard]] const char* received();

/// The widest of them, measured — never fewer than the four columns the English
/// labels take, so that a language with shorter words does not narrow the block.
[[nodiscard]] int labelWidth();

/// The whole column a label stands in: the indent in front of it, the label, and
/// the " : " behind. What a row's remaining width is measured from.
[[nodiscard]] int labelColumn();

/// One label as it is drawn, padded into that column.
[[nodiscard]] std::string labelCell(const std::string& label);

}  // namespace header_labels

}  // namespace amberedit::ui::screens::message_read
