#pragma once

#include <cstddef>
#include <cstdint>

#include "ui/app_state.hpp"

namespace amberedit::ui {

/// How long an operation runs before the box counting it goes up, and how often
/// it is drawn again once it has.
///
/// The delay is what keeps three marked messages from flashing a box on the
/// screen and taking it off again: an operation nobody had time to read is an
/// operation nobody has to be told about. The tick is the other half of the same
/// question — a frame per message would spend a run of a hundred thousand
/// drawing rather than deleting, and ten a second is as fast as a number is
/// worth reading anyway.
///
/// Not settings. What either would be worth to a user is a box that behaved
/// differently, and there is nothing behind these two numbers to prefer.
constexpr Millis kProgressDelayMs = 200;
constexpr Millis kProgressTickMs = 100;

/// One long run over the marked messages, counted on the screen and breakable
/// off with Escape.
///
/// **One of these stands for the whole operation and not for one pass of it.**
/// A copy reads every marked message off the base being read before it writes
/// any of them into the other area — there is one base open at a time — and a
/// move takes them out of this one afterwards, so what the user asked for once
/// is two or three walks over the same set. `begin()` opens each of them, and
/// the box stays on the screen across all of them: a delay begun afresh at every
/// pass would take it off in the middle of the operation it is about.
///
/// ```
/// ProgressRun run(state);
/// run.begin(Doing::Delete, uids.size());
/// for (...) {
///     if (!run.step()) break;  // Escape
///     ...
/// }
/// ```
///
/// `step()` is called **before** the message it stands for is worked on, so the
/// number on the screen names the one being done rather than the one just
/// finished, and a run broken off has not touched the message it was counting.
/// What a broken run leaves behind is the caller's to settle — see
/// `message_read::deleteMarked()` and `passOnMarked()`, which leave every
/// message they did not reach marked.
class ProgressRun {
public:
    explicit ProgressRun(AppState& state) : state_(state) {}

    /// Takes the box off the screen. The frame without it is the loop's own
    /// next one: whatever asked for the run is still to finish putting the
    /// screen back together — the reader lands on a message, the list on a row —
    /// and a frame drawn from here would be drawn in the middle of that.
    ~ProgressRun() { state_.progress.reset(); }

    ProgressRun(const ProgressRun&) = delete;
    ProgressRun& operator=(const ProgressRun&) = delete;
    ProgressRun(ProgressRun&&) = delete;
    ProgressRun& operator=(ProgressRun&&) = delete;

    /// Opens a pass of `total` messages, saying what it does to each. The count
    /// starts again and the box keeps whatever it had: how long the operation
    /// has been running, and whether it is on the screen yet.
    ///
    /// `breakable` false is the pass Escape does not stop — the second half of a
    /// move, which takes out of this area what is already written into the other
    /// one. The key is still read, so that what was typed during the run is
    /// dropped rather than answered by the screen coming back; it is the
    /// stopping that such a pass does not offer, and the box leaves the line
    /// saying so out.
    void begin(AppState::Progress::Doing doing, size_t total, bool breakable = true) {
        if (!state_.progress) {
            AppState::Progress fresh;
            fresh.started = state_.monotonicMs();
            state_.progress = fresh;
        }
        state_.progress->doing = doing;
        state_.progress->done = 0;
        state_.progress->total = static_cast<uint32_t>(total);
        state_.progress->breakable = breakable;
    }

    /// Counts one message of the pass and says whether the run goes on.
    ///
    /// False is Escape: the user asked for the run to stop, and what has been
    /// done to the messages before this one stands. The keyboard is read on the
    /// frames the box is drawn on and not in between — reading it is a call into
    /// the terminal, and a run of a hundred thousand messages would make that
    /// call a hundred thousand times to answer a key nobody pressed.
    [[nodiscard]] bool step() {
        if (!state_.progress) return true;  // begin() was never called
        AppState::Progress& progress = *state_.progress;
        ++progress.done;

        const Millis now = state_.monotonicMs();
        if (!progress.shown) {
            if (now - progress.started < kProgressDelayMs) return true;
            progress.shown = true;
        } else if (now - progress.drawn < kProgressTickMs) {
            return true;
        }
        progress.drawn = now;
        state_.redraw();
        // Asked whether or not it is acted on: asking is also what takes what
        // was typed during the run off the terminal.
        const bool stop = state_.breakRequested();
        return !stop || !progress.breakable;
    }

private:
    AppState& state_;
};

}  // namespace amberedit::ui
