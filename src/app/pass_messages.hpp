#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>

#include "app/area_manager.hpp"
#include "domain/area.hpp"

/// A run of messages carried from one area into another — what the reader's Copy
/// and Move come to once the set and the target have been picked.
///
/// **It is here and not on the screen because of what it has to juggle.** One
/// base is open at a time, so the messages have to be read off the source before
/// the target can be opened at all; a whole area's worth of them read first is a
/// whole area's worth of memory, and a hundred thousand marked messages would be
/// a third of a gigabyte before a single one was written. So the run goes round —
/// a bounded chunk read here, the target opened and that chunk written, the
/// source opened again — and which base is open when, how much is held at once
/// and where the walk carries on from are questions about message bases rather
/// than about what is drawn. The screen is left with what it is for: asking
/// which messages, saying how far along the run is, and putting the reader back
/// where it belongs afterwards.
namespace amberedit::app {

/// How much of a run is held in memory at once: so many messages, or so many
/// bytes of them, whichever is reached first.
///
/// **The bytes are the point and the count is the guard.** A draft costs about
/// twice the text it carries — a line is a `std::string` of its own — so an area
/// of ordinary echomail comes to some three kilobytes a message, which the byte
/// bound is what holds down. The count bounds what a chunk of very short
/// messages costs in per-message overhead, which the byte total does not see.
///
/// Larger chunks cost fewer swaps and more memory. These two are the middle of
/// it: a run of a hundred thousand ordinary messages swaps a couple of hundred
/// times — a swap being one index read, which is nothing beside writing the
/// messages — and never holds more than a few megabytes.
constexpr size_t kChunkMessages = 512;
constexpr size_t kChunkBytes = 4u << 20;

/// What is to be carried where, and what is to happen between one message and
/// the next.
///
/// Built for one call and not kept: `uids` is a reference to the caller's own
/// set. A run is asked for over the whole of what somebody marked, which on a
/// busy area is a hundred thousand of them, and copying that set in to say which
/// messages are meant would cost as much memory as a chunk of the messages
/// themselves.
struct PassRequest {
    const domain::AreaConfig& source;
    const domain::AreaConfig& target;
    /// The messages, by UID — the only name that survives the renumbering a
    /// message taken out of the middle of an area causes. A UID the source does
    /// not hold names nothing and is passed over.
    const std::set<uint32_t>& uids;
    /// Whether the UID of each message that went in is to be handed back.
    /// **A move alone asks for it**: it is what says afterwards which messages
    /// may be taken out of the source, and a copy that gathered the same set
    /// would be collecting tens of bytes a message in order to throw them away.
    bool remember{false};
    /// Called before each message is carried over — the caller's chance to show
    /// how far along the run is, and to say whether it goes on. False stops it
    /// there, with the message it was called for untouched. An empty function is
    /// a run nobody can interrupt, which is what a caller with nothing to draw
    /// on wants.
    std::function<bool()> onMessage;
};

/// What the run came to.
struct PassReport {
    /// How many messages the target took.
    size_t written{0};
    /// Which of them, by UID, where `remember` asked. Only what is safely in the
    /// other area is in here: a message the target refused is one that must not
    /// be taken out of anywhere.
    std::set<uint32_t> stored;
    /// Whether `onMessage` stopped the run rather than it running out of
    /// messages. What had gone over by then is in the other area and counted
    /// above — there is nothing to undo and nothing here that could.
    bool stopped{false};
};

/// Carries the messages `uids` names from the source into the target, a chunk at
/// a time, and leaves the **target** area's counts refreshed where anything went
/// in.
///
/// Copying into the area being read is the one case with no swap in it: the
/// messages are appended to the very base they are read from. The walk still
/// ends where the area ended when the run began, or it would go on copying its
/// own copies for as long as there was room on the disk.
///
/// **The caller must be holding no pointer into any open base**, and must open
/// the area it was reading again afterwards: the manager keeps one base open at
/// a time and this swaps between two. An area that will not open is the end of
/// the run and not an answer of its own — there is nowhere on the screen to say
/// so, and what did go over is in the report either way.
[[nodiscard]] PassReport passMessages(AreaManager& manager, const PassRequest& request);

}  // namespace amberedit::app
