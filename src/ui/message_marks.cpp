#include "ui/message_marks.hpp"

#include <set>

#include "ui/app_state.hpp"

namespace amberedit::ui::marks {
namespace {

/// The UID that identifies the message at `number`, or zero where there is no
/// such message — no area open, or a number past the end of it. Zero is never
/// put in the set: it is what the port answers with for a message that is not
/// there, and a set holding it would mark whatever took that place next.
uint32_t uidAt(const AppState& state, uint32_t number) {
    if (state.base == nullptr || number == 0 || number > state.messageCount) return 0;
    return state.base->uidOf(number);
}

/// Walks the area from `first` to `last` inclusive and hands each message's UID
/// to `visit`. The one loop the four sweeps below are written in, so that none of
/// them can go one message further than the others.
template <typename Visit>
void walk(const AppState& state, uint32_t first, uint32_t last, Visit visit) {
    if (state.base == nullptr) return;
    for (uint32_t number = first; number <= last && number <= state.messageCount;
         ++number) {
        if (const uint32_t uid = uidAt(state, number); uid != 0) visit(uid);
    }
}

}  // namespace

bool isMarked(const AppState& state, uint32_t number) {
    const uint32_t uid = uidAt(state, number);
    return uid != 0 && state.marks.count(uid) != 0;
}

void toggle(AppState& state, uint32_t number) {
    const uint32_t uid = uidAt(state, number);
    if (uid == 0) return;
    if (state.marks.erase(uid) == 0) state.marks.insert(uid);
}

void markAll(AppState& state) {
    walk(state, 1, state.messageCount,
         [&state](uint32_t uid) { state.marks.insert(uid); });
}

void unmarkAll(AppState& state) {
    state.marks.clear();
}

void toggleAll(AppState& state) {
    // Built beside the old set rather than in it: a message toggled into the set
    // while the same set is being walked is a message the walk would meet again
    // and toggle back out.
    std::set<uint32_t> inverted;
    walk(state, 1, state.messageCount, [&state, &inverted](uint32_t uid) {
        if (state.marks.count(uid) == 0) inverted.insert(uid);
    });
    state.marks = std::move(inverted);
}

void markNewer(AppState& state, uint32_t number) {
    // Zero names no message, so there is nothing for "after it" to mean — and
    // marking the whole area would be the one answer the user did not pick.
    if (number == 0) return;
    walk(state, number + 1, state.messageCount,
         [&state](uint32_t uid) { state.marks.insert(uid); });
}

void markOlder(AppState& state, uint32_t number) {
    if (number == 0) return;
    walk(state, 1, number - 1, [&state](uint32_t uid) { state.marks.insert(uid); });
}

}  // namespace amberedit::ui::marks
