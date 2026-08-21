#pragma once

#include <string>
#include <vector>

#include "nodelist/node_entry.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

#include "ui/app_state.hpp"

/// Writing a message: the header block and the text below it, on one screen.
///
/// The text is there from the moment the message is begun — the quote of what
/// is being answered, or what the template makes of a new message — and the
/// header stands over it, filled in field by field. `AppState::composeInHeader`
/// is which of the two the typing goes into: a new message opens in the header,
/// where the recipient has to be named, and a reply opens in the text.
/// `Ctrl-U` goes back up into the header, `Enter` off its last field comes back
/// down.
namespace amberedit::ui::screens::compose {

/// The stops of the header, in the order Tab walks them. The To address is
/// skipped in echomail, where it addresses nobody.
///
/// The last of them is not a field but a button: the message's attributes,
/// which are set in a dialog rather than typed. Everything before it is typed
/// into, which is what `kAttributes` doubles as the count of — Enter walks those
/// and hands the typing down to the text off the last, the button being Tab's.
enum Field {
    kFromName,
    kFromAddr,
    kToName,
    kToAddr,
    kSubject,
    kAttributes,
    kFieldCount,
};

/// Starts a new message in the area the reader is in, and opens the compose
/// screen on it, the cursor in the header — a new message has a recipient to
/// name before there is anything to say to them.
void startNew(AppState& state);

/// Starts a reply to the message in the reader, the cursor in the text: the
/// header is filled in from the message being answered, and the quote is what
/// the answer is written into. Does nothing where there is no message — an
/// empty area is the only screen with none, and the menu's Reply button is dead
/// there.
///
/// Where the message names an echo of its own — an `AREA:` line at the head of
/// it, and `areareplydirect` on for the area it is being read in — the answer
/// goes there instead, exactly as `startReplyTo()` would have taken it. The
/// reader stays where it is either way, and saving or dropping comes back to it.
void startReply(AppState& state);

/// Starts the same reply, to be written into `target` rather than into the area
/// the message was read in — what the reader's `n` asks for, once the dialog
/// has been answered with an area.
///
/// Everything else about it is an ordinary reply: the quote, the subject
/// carried over, the cursor in the text. What the target decides is
/// the sender's AKA and whether there is a recipient to address, and what the
/// move adds is the template's @moved lines. The reader underneath keeps its
/// own area, and saving or dropping the message comes back to it.
void startReplyTo(AppState& state, const domain::AreaConfig& target);

/// Starts a message passing the one in the reader on into `target` — what the
/// reader's `m` asks for, once the dialog has been answered with an area.
///
/// It is a new message and not an answer: addressed to the area it is going to
/// rather than to whoever wrote the message being forwarded, carrying no reply
/// link back to it, and quoting nothing. What it takes from that message is its
/// subject and — where the template's @message stands, between its @forward
/// lines — its text. `target` may be the area being read; a forward into it is
/// then an ordinary new message and nothing is swapped to store it.
void startForwardTo(AppState& state, const domain::AreaConfig& target);

/// Opens the editor on the message in the reader itself, to be written back
/// over the one in the base rather than beside it — what the reader's `change`
/// asks for, once whatever confirmation was owed has been answered.
///
/// It is not a template's message: the text is the message's own, the header
/// block holds its fields and its attributes, and it keeps its date, its place and
/// its number when it is stored. `notice` puts the template's `@Changed` lines
/// at the head of it, which is what a message changed by somebody other than
/// whoever wrote it gets — the only mark a change leaves of itself.
void startChange(AppState& state, bool notice);

/// The message being written: the header block and the text under it. The state
/// is not const because the frame is what decides where the header's fields and
/// the Change button beside the attributes landed, and a click is tested
/// against that rather than against a second copy of the same arithmetic.
term::Element render(AppState& state);
bool handleEvent(AppState& state, const term::Event& event);

/// Puts the editor's context menu up — what the button in the top-right corner
/// does. What it holds is `compose_menu`.
void openMenu(AppState& state);

/// Runs one of those commands, which is what the shell does once the menu has
/// been answered. Both are things a key on this screen does as well.
void runMenuCommand(AppState& state, config::MenuCommand command);

/// Puts the cursor into the header, on the field it was last in — what `Ctrl-U`
/// asks for.
void editHeader(AppState& state);

/// Whether the message may be stored: it needs a sender's address, and a
/// recipient's in netmail. false puts the cursor on the field at fault, in the
/// header, and the message is not stored.
[[nodiscard]] bool addressesReady(AppState& state);

/// Stores what the editor holds and goes back to the reader, on the message
/// just written. Called when the save confirmation is answered yes. A message
/// whose addresses are not there is not stored; the cursor lands on the field
/// at fault instead.
void saveMessage(AppState& state);

/// Leaves the editor with nothing stored, the answer to the other question.
void dropMessage(AppState& state);

/// Carries out the `CC:` and `XC:`/`XP:` commands the message carries and then
/// stores it — the Process answer to the question storing it asked.
///
/// The copies are made first and the message is stored after them, each copy
/// carrying the message as it now reads: the lists `compose_cc_list` and
/// `compose_xc_list` ask for stand where the commands were written, and every
/// copy is closed with the tearline, the origin, the AKA and the charset of the
/// area it goes into.
///
/// It can stop halfway. A `CC:` naming somebody the nodelist does not answer
/// for outright puts the nodelist box up, and `useCarbonCopy()` is what takes
/// it up again from there.
void processCopies(AppState& state);

/// Stores the message with those lines left in it as text — the Ignore answer
/// to the same question, and what Esc over it comes to. Nothing is copied
/// anywhere and nothing is taken out of the message.
void ignoreCopies(AppState& state);

/// The answer to the nodelist box a `CC:` line put up: the node the copy is
/// for, or nullptr where the box was closed without picking one — in which case
/// that copy is not made, the line naming them stays in the message, and the
/// box afterwards says so. Either way the rest of the commands are carried out
/// and the message is stored.
void useCarbonCopy(AppState& state, const nodelist::NodeEntry* node);

/// Puts a file the import dialog has read into the message.
///
/// Where it goes is where the cursor is, and the block goes in as whole lines:
/// at the cursor when it stands at the start of a line, and after that line when
/// it stands anywhere else — a file is not something to bury in the middle of a
/// sentence, and the line being written is left as it was written. The cursor
/// comes to rest under the block, which is where the writing goes on from.
///
/// The typing comes down out of the header first where it was up there: what has
/// just been read goes into the message, and there is nowhere in the block for
/// it to go.
void insertImported(AppState& state, const std::vector<std::string>& lines);

/// Fills in the To row from a node picked out of the nodelist — what Enter on a
/// row of that box comes to.
///
/// **Both halves come from the node**, whichever of them the box was opened to
/// answer. The message is addressed to the node that was picked, and half a row
/// out of the nodelist beside half a row out of a search field is not that
/// node: `Schroeter` is how a node is found and `Ulrich Schroeter` is who is
/// there, and a partial address typed to look around at is not the address of
/// the node the user then chose from what it showed.
///
/// The name goes in **as the nodelist spells it**, which is the spelling the
/// system at the other end matches on.
///
/// Filling the address in also settles which AKA the message goes out under, as
/// leaving the field by hand would. The cursor comes to rest on the subject:
/// the To row is whole once the node has answered it, and there is nothing left
/// on it to stand on.
void useNode(AppState& state, AppState::NodelistView::Purpose purpose,
             const nodelist::NodeEntry& node);

/// Rows the editor has to write in, the header block and the chrome taken off.
[[nodiscard]] int editorRows(const AppState& state);

}  // namespace amberedit::ui::screens::compose
