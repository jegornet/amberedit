#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <vector>

#include "ports/i_msgbase.hpp"

/// A set of messages taken out of the area being read — what the reader's Delete
/// and the second half of a Move come to once the set has been picked.
///
/// **It is here and not on the screen for `pass_messages.*`'s reason**: how an
/// area is walked, and how much of one is handed to the base at a time, are
/// questions about message bases. A delete is cheap to write and dear to
/// prepare — every format re-reads what it writes against under the lock, and
/// JAM rebuilds its whole table of active messages doing it — so a set taken out
/// a message at a time costs the area over again for each of them, and a run of
/// a hundred thousand is minutes of exactly that. Which is a fact about the
/// formats and not about what is drawn, so the screen is left with what it is
/// for: asking which messages, counting the run, and putting the reader back
/// where it belongs afterwards.
namespace amberedit::app {

/// How many messages go to the base in one call.
///
/// **A chunk is one call and one lock**, so the number is both how much of that
/// preparing is shared and how long the area is held unwritable at a stretch.
/// Paid once per chunk the preparing stops mattering — a run over an area of
/// twenty thousand goes from seconds to a tenth of one — and what is left of it
/// shrinks as the chunk grows, which is why the number is thousands rather than
/// dozens.
///
/// It stops there because nothing above it is worth having: two thousand
/// messages are a few milliseconds of the base being locked and of the caller's
/// `onMessage` going unasked, and a chunk long enough to be felt as either would
/// be buying a share of a cost that is already gone.
///
/// Not a setting, for `kChunkMessages`'s reason: what a user could want from it
/// is a run that went faster, which is what it is set to.
constexpr size_t kRemoveChunk = 2048;

/// Which messages are to go, and what is to happen between one and the next.
///
/// Built for one call and not kept: `uids` is a reference to the caller's own
/// set, as `PassRequest::uids` is and for the same reason — a set of a hundred
/// thousand marks copied in to say which messages are meant would cost as much
/// as the run itself.
struct RemoveRequest {
    /// The messages, by UID — the only name that survives the renumbering each
    /// chunk causes. A UID the area does not hold names nothing and is passed
    /// over.
    const std::set<uint32_t>& uids;
    /// Called before each message is taken out — the caller's chance to show how
    /// far along the run is, and to say whether it goes on. False stops it
    /// there, with the message it was called for left standing: it is not in the
    /// chunk that goes to the base. An empty function is a run nobody can
    /// interrupt, which is what a caller with nothing to draw on wants.
    std::function<bool()> onMessage;
};

/// What the run came to.
struct RemoveReport {
    /// Which messages are gone, by UID. What the caller had marked and no longer
    /// has any reason to: everything else it marked is still in the area.
    std::vector<uint32_t> removed;
    /// Whether `onMessage` stopped the run rather than it running out of
    /// messages. What had already gone is gone — there is nothing to undo and
    /// nothing here that could.
    bool stopped{false};
};

/// Takes the messages `uids` names out of `base`, a chunk at a time.
///
/// **The area is walked backwards**, and everything else here follows from it:
/// taking a message out moves the number of every message after it, so a walk
/// running forwards would step over whatever moved up into the place of the one
/// just removed. Backwards, every number the walk has yet to reach still names
/// the message it named when the run began, whatever chunks have gone out from
/// above it — which is what lets the numbers be gathered and handed over in
/// bulk at all.
///
/// A base that will not be written ends the run: a read-only area answers the
/// same way for every chunk after the first, and the report says nothing went.
[[nodiscard]] RemoveReport removeMessages(ports::IMsgBase& base,
                                          const RemoveRequest& request);

}  // namespace amberedit::app
