#pragma once

#include <functional>
#include <memory>
#include <string>

#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"

namespace amberedit::ui::term {

/// The terminal, for as long as the interface is up.
///
/// Constructing one takes the terminal over — raw mode, the alternate screen,
/// the mouse, the cursor — and destroying one gives it all back, in the reverse
/// order and whatever the program is doing at the time. Nothing else in AmberEdit
/// touches ncurses, and nothing else needs to.
///
/// The loop this is meant to be driven by lives in the caller rather than here,
/// so that what is drawn and what a keystroke means stay in one place:
///
/// ```
/// Terminal terminal;
/// while (terminal.running()) {
///     terminal.draw(document());
///     dispatch(terminal.poll());
/// }
/// ```
class Terminal {
public:
    /// `altLetters` names the letters bound with Alt held. They have to be
    /// asked for by name: on a terminal that knows no keyboard protocol, Alt+F
    /// arrives as an ESC in front of an `f`, which is also what pressing Escape
    /// and then `f` looks like — so a letter is claimed only where the layout
    /// binds it, and Escape keeps the rest. `altBackspace` says the same of
    /// Alt with Backspace, which arrives the same ambiguous way.
    explicit Terminal(std::string altLetters = "", bool altBackspace = false);
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;
    Terminal(Terminal&&) = delete;
    Terminal& operator=(Terminal&&) = delete;

    /// Lays the document out over the whole terminal and puts it on the screen.
    /// Only what changed since the last frame is actually written out, which is
    /// what ncurses is for and what makes this bearable over a slow link.
    void draw(const Element& document);

    /// Waits for the next event. Never returns Kind::None: a resize comes back
    /// as Event::Resize and anything the terminal sent that nothing here binds
    /// is swallowed rather than passed on.
    [[nodiscard]] Event poll();

    /// Gives the terminal back to whatever `work` runs on it, and takes it again
    /// when that returns.
    ///
    /// For the one thing AmberEdit does that is not drawing: the user's own
    /// shell, which wants the terminal the way they found it — their prompt on
    /// the normal screen, their own tty modes, no mouse reporting and nothing
    /// asking for modified keys. Everything the constructor put on is taken off
    /// here in the destructor's order and put back in the constructor's, and the
    /// screen is forgotten on the way in so that the next `draw()` paints all of
    /// it rather than a difference against what the shell scrolled away.
    ///
    /// `work` is called once and its own failures are its own: this hands the
    /// terminal over and takes it back, and nothing between the two is looked at.
    void handOver(const std::function<void()>& work);

    /// Throws away whatever has been typed but not yet read.
    ///
    /// For the one case where the application was busy rather than waiting: keys
    /// pressed while the areas were being rescanned were aimed at a list that
    /// was being rebuilt underneath them, and acting on them once it comes back
    /// would open an area nobody chose.
    void flushInput();

    /// Ends the loop after the current pass. Kept as a flag rather than acted on
    /// at once so that whatever asked to quit can finish what it was doing.
    void exit() { running_ = false; }
    [[nodiscard]] bool running() const { return running_; }

    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;

    /// Where the cursor should sit, or nowhere at all. Only the message editor
    /// wants one; every other screen leaves it hidden.
    void showCursor(int x, int y);
    void hideCursor();

    /// What the terminal is being written in — UTF-8 unless the user asked for
    /// something else. Worth reporting when it is not what they expected.
    [[nodiscard]] const std::string& codeset() const;

private:
    /// Brings the frame buffer to the terminal's current size, if it has
    /// changed, and tells ncurses to forget what it thinks is on the screen.
    ///
    /// Done as the resize is read rather than as the next frame is drawn, so
    /// that width() and height() are right before anything asks them. Laying out
    /// against the old width and correcting on the frame after would leave the
    /// screen wrong until the user happened to press a key.
    void syncSize();

    Screen screen_;
    bool running_{true};
    bool cursorVisible_{false};
    int cursorX_{0};
    int cursorY_{0};
};

}  // namespace amberedit::ui::term
