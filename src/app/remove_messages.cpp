#include "app/remove_messages.hpp"

#include <cstdint>
#include <vector>

namespace amberedit::app {

RemoveReport removeMessages(ports::IMsgBase& base, const RemoveRequest& request) {
    RemoveReport report;
    if (request.uids.empty()) return report;

    // The chunk being gathered: the numbers to hand the base, and the UIDs of
    // the same messages, which are what the caller knows them by. Two lists
    // rather than one of pairs — the numbers go to the base as they stand.
    std::vector<uint32_t> numbers;
    std::vector<uint32_t> marked;
    numbers.reserve(kRemoveChunk);
    marked.reserve(kRemoveChunk);

    // What a full chunk comes to: one call, one lock, and the messages counted
    // as gone once they are. False is a base that would not be written, which
    // ends the run — every further chunk is the same answer.
    const auto takeOut = [&] {
        if (numbers.empty()) return true;
        const bool gone = base.removeAll(numbers).has_value();
        if (gone) {
            report.removed.insert(report.removed.end(), marked.begin(), marked.end());
        }
        numbers.clear();
        marked.clear();
        return gone;
    };

    for (uint32_t number = base.count(); number >= 1; --number) {
        const uint32_t uid = base.uidOf(number);
        if (request.uids.count(uid) == 0) continue;
        // Asked before the message is gathered, so that a run stopped here has
        // left it standing: it is not in the chunk handed over below.
        if (request.onMessage && !request.onMessage()) {
            report.stopped = true;
            break;
        }
        numbers.push_back(number);
        marked.push_back(uid);
        if (numbers.size() >= kRemoveChunk && !takeOut()) return report;
    }
    static_cast<void>(takeOut());
    return report;
}

}  // namespace amberedit::app
