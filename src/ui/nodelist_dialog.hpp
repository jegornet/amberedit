#pragma once

#include <optional>
#include <string>

#include "nodelist/node_entry.hpp"
#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// The nodelist, over the reader: who is at an address, and where a sysop is —
/// what Ctrl-N opens.
///
/// **The list is the whole nodelist, and the Lookup line is where the cursor
/// goes.** It does not filter: a node is worth as much for its neighbours as
/// for itself — the net it stands in, the hub above it, the points under it —
/// and a list that showed only what matched would take exactly that away. The
/// line is a place to jump to, and Enter jumps to the next place the same text
/// finds.
///
/// What it looks for is decided by what was typed, and by nothing else the user
/// has to say:
///
/// - Anything that reads as the beginning of an address — `2`, `2:240`,
///   `2:240/1120`, `2:240/1120.8` — goes to the first node under it. An address
///   nobody has scrolls to where it would stand, so a sysop asking about
///   `2:240/1121` can see that 1120 and 1200 are there and it is not.
/// - Anything else is a sysop's name, matched anywhere inside it: the whole
///   name, a surname on its own, or three letters out of the middle of one.
///
/// It opens on the address of whoever wrote the message being read, which is
/// the question a nodelist is opened to answer nine times in ten — and the
/// first character typed replaces that address rather than being added to it,
/// once, because what stands there is an answer already given and not the
/// beginning of the next question.
///
/// The box is a modal of a fixed size, as the import and export boxes are: the
/// Lookup line stands in its top rule, the node under the cursor at its head,
/// the list under a rule below that, and the nodelist the node came from in the
/// bottom rule. A list longer than the box carries the reader's own scrollbar
/// down its rightmost column. Esc closes it; Backspace only ever edits the line.
namespace amberedit::ui::nodelist_dialog {

/// Opens it on the sender of the message being read — what Ctrl-N does. The
/// compiled nodelist is read the first time this is called and kept afterwards.
///
/// It opens even where there is no nodelist to show: the box says so along its
/// bottom edge, which is the only place there is to say it. A key that quietly
/// did nothing would leave the user to guess whether Ctrl-N is a key at all.
void open(AppState& state);

/// Opens it to pick a node for the message being written, looking for `lookup`.
///
/// `purpose` says both what Enter on a row fills in and what the list holds:
/// **a name being looked up shows what it found, closest first**, since
/// somebody who typed a name is asking which node is theirs and the nodes
/// around the answer say nothing about that. An address being looked up shows
/// the nodelist as Ctrl-N does, the neighbours being half of what an address is
/// asked about.
void openFor(AppState& state, AppState::NodelistView::Purpose purpose,
             const std::string& lookup);

/// Draws it over `background`.
[[nodiscard]] term::Element render(AppState& state, term::Element background);

/// What answering a key came to.
enum class Outcome {
    /// The key was dealt with inside the box, which may have closed itself —
    /// `state.nodelistView` is empty then, and there is nothing to do but stop
    /// sending it events.
    Ignored,
    /// A node was picked. The box is still open and `currentNode()` is what was
    /// picked, so that the caller can take it before putting the box away.
    Picked,
};

/// Answers a key or a click.
Outcome handleEvent(AppState& state, const term::Event& event);

/// The node under the cursor, or nothing where the box is showing none.
[[nodiscard]] std::optional<nodelist::NodeEntry> currentNode(const AppState& state);

}  // namespace amberedit::ui::nodelist_dialog
