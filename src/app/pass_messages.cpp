#include "app/pass_messages.hpp"

#include <string>
#include <vector>

#include "app/message_builder.hpp"
#include "domain/message.hpp"
#include "ports/i_msgbase.hpp"

namespace amberedit::app {
namespace {

/// What a draft costs to hold, near enough to bound a chunk by: the text and the
/// control lines are all of it that grows with the message.
size_t weightOf(const domain::MessageDraft& draft) {
    size_t bytes = draft.from.size() + draft.to.size() + draft.subject.size();
    for (const std::string& line : draft.kludges) bytes += line.size();
    for (const std::string& line : draft.lines) bytes += line.size();
    return bytes;
}

/// One chunk of the run as the source holds it, in the order the messages stand
/// in the area, with the UID of each beside it.
///
/// Read out before any of them is written: opening the target closes the source,
/// so a walk that read as it wrote would be reading from a base that is gone.
/// The UIDs are what say afterwards which of them went in — the numbers will
/// have moved by then, the UIDs will not.
struct Chunk {
    std::vector<domain::MessageDraft> drafts;
    std::vector<uint32_t> uids;
};

/// The messages `uids` names from `from` onward, up to what one chunk holds,
/// stopping at `last`. `from` comes back as the number to carry on from, so that
/// the area is walked once however many chunks it takes.
Chunk chunkFrom(ports::IMsgBase& base, const std::set<uint32_t>& uids, bool addressed,
                uint32_t last, uint32_t& from) {
    Chunk chunk;
    size_t bytes = 0;
    for (; from <= last; ++from) {
        const uint32_t uid = base.uidOf(from);
        if (uids.count(uid) == 0) continue;
        // Tested before the message is read rather than after, so that a chunk
        // always holds at least one: a message bigger than the whole budget is a
        // chunk of its own, where the other way round it would be a chunk of
        // nothing and a run that quietly did nothing at all.
        if (chunk.drafts.size() >= kChunkMessages || bytes >= kChunkBytes) break;
        chunk.drafts.push_back(copyOf(base.header(from), base.body(from), addressed));
        chunk.uids.push_back(uid);
        bytes += weightOf(chunk.drafts.back());
    }
    return chunk;
}

/// Whether the two name the same area. Tag and path together, as everywhere else
/// here.
bool sameArea(const domain::AreaConfig& one, const domain::AreaConfig& other) {
    return one.tag == other.tag && one.path == other.path;
}

}  // namespace

PassReport passMessages(AreaManager& manager, const PassRequest& request) {
    PassReport report;
    if (request.uids.empty()) return report;

    const bool here = sameArea(request.source, request.target);
    const bool addressed = request.target.hasAddressedRecipient();
    const auto carryOn = [&request] { return !request.onMessage || request.onMessage(); };

    ports::IMsgBase* source = manager.openArea(request.source).value_or(nullptr);
    if (source == nullptr) return report;

    // Where the walk ends, read once and never again: copying into the area
    // being read appends to the very base being walked, and a walk that followed
    // the end of it would copy its own copies.
    const uint32_t last = source->count();
    uint32_t from = 1;

    while (!report.stopped) {
        const Chunk chunk = chunkFrom(*source, request.uids, addressed, last, from);
        if (chunk.drafts.empty()) break;

        if (here) {
            for (const auto& draft : chunk.drafts) {
                if (!carryOn()) {
                    report.stopped = true;
                    break;
                }
                if (!source->write(draft)) continue;
                ++report.written;
            }
            continue;
        }

        // The swap this chunk costs. Nothing may be left pointing at the source
        // while the target takes its place.
        source = nullptr;
        if (const auto into = manager.openArea(request.target)) {
            for (size_t at = 0; at < chunk.drafts.size(); ++at) {
                // A run stopped here has written part of the set, and a move
                // still takes that part out of the source afterwards: what is in
                // the other area is there whether or not the rest followed it,
                // and leaving it in both would be the one outcome nobody asked
                // for.
                if (!carryOn()) {
                    report.stopped = true;
                    break;
                }
                if (!(*into)->write(chunk.drafts[at])) continue;
                ++report.written;
                if (request.remember) report.stored.insert(chunk.uids[at]);
            }
        } else {
            // The target will not open. It will not open for the next chunk
            // either, and every one of them would swap away from the source to
            // find that out again.
            report.stopped = true;
        }

        source = manager.openArea(request.source).value_or(nullptr);
        if (source == nullptr) {
            // The area the run is reading will not open again. There is nothing
            // left to read and nothing this can do about it; what went over is
            // in the report, and the caller finds the same thing the moment it
            // opens that area itself.
            report.stopped = true;
            return report;
        }
    }

    // The area list counts them once the run is over rather than once a chunk:
    // so many messages more in an area nobody has read, and so many unread more.
    // Nothing went over, nothing to count.
    if (report.written != 0) manager.refreshArea(request.target);
    return report;
}

}  // namespace amberedit::app
