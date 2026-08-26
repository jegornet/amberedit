#pragma once

#include "ui/app_state.hpp"

/// Reading the base again after another program has had the terminal.
///
/// What it is for: the shell behind `reader.shell` and the utilities
/// `extern_util0` and its nine fellows name are handed the screen, and what
/// they do with it is their own — including writing to the very area being
/// read. A message comes back with different attributes, an area with more
/// messages in it than it had, or with fewer, or packed and renumbered
/// throughout. Until this runs, the reader is showing what the base held before
/// the program started.
///
/// **A re-read is not enough: the area is reopened.** Every format driver reads
/// its index into memory when the area is opened and re-reads it only under the
/// write lock — `SquishBase`'s index, `JamBase`'s active table, `SdmBase`'s
/// directory listing — so a base another program has written to goes on
/// answering from the index it was opened with. There is no reindex on
/// `IMsgBase` to ask for, and opening the area again is the whole of what one
/// would do.
///
/// The link handler is not one of these. A browser does not write to a message
/// base, and reopening one after every click on an address would be a file
/// opened for nothing.
namespace amberedit::ui::after_handover {

/// Reopens the area being read and loads the message on the screen again.
///
/// The message is found by the UID it was read under rather than by the number
/// it stood at, which is the same rule a lastread mark follows and for the same
/// reason: a program that packed the area moved every message in it. One that
/// has since been deleted lands on the nearest surviving message, as a mark on
/// a deleted message does.
///
/// What belongs to the message is read again; what belongs to the reader —
/// where the text was scrolled to, a twit shown after all, what a search lit —
/// is left where the user had it. Nothing here touches a message being written:
/// the editor's draft is not the base's.
///
/// `rescan_on_return` is what also rescans every other area. Off, this is one
/// file opened however long the area list is.
void refresh(AppState& state);

}  // namespace amberedit::ui::after_handover
