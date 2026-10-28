#pragma once

#include <chrono>
#include <cwchar>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

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

    /// Whether Escape is among whatever has been typed and not yet read, taking
    /// the lot off the queue either way.
    ///
    /// The one way in for a key while the loop is not the thing waiting for one:
    /// a run over a whole area's worth of marked messages blocks inside the call
    /// doing it, and Escape is what breaks such a run off. Everything else that
    /// was typed meanwhile is dropped, exactly as `flushInput()` drops what was
    /// typed during a rescan — those keys were aimed at a screen nobody was
    /// answering for.
    [[nodiscard]] bool escapePressed();

    /// Asks the terminal to put `base64` — the text, already encoded — on the
    /// clipboard of the machine the terminal runs on: OSC 52, which is what
    /// lets a copy made over ssh land on the desk the user is sitting at.
    ///
    /// **Nothing comes back to say whether it did.** A terminal that does not
    /// know the sequence, or has been told not to act on it, ignores it, and
    /// there is no question to ask afterwards — so nothing is asked, as nothing
    /// is asked about a palette entry a truecolor theme was lent.
    void setHostClipboard(std::string_view base64);

    /// What asking the terminal for its clipboard came to.
    struct HostClipboard {
        enum class Outcome {
            Answered,     ///< `base64` is what the clipboard holds
            Unsupported,  ///< the terminal said it will not answer such a question
            NoAnswer,     ///< nothing came back before the wait ran out
            Cancelled,    ///< Escape, pressed while the answer was being waited for
        };
        Outcome outcome{Outcome::NoAnswer};
        std::string base64;
    };

    /// Asks the terminal what that clipboard holds, and waits up to `waitMs`
    /// for the answer — long enough for a terminal that asks the user first,
    /// as iTerm2 does every time unless told otherwise. Escape ends the wait.
    ///
    /// **The question is followed by a second one, DA1**, which every terminal
    /// answers at once and in order. One that answers it first, without
    /// counting clipboard access (`52`) among what it can do, has passed over
    /// the clipboard question and will never answer it — WezTerm does that —
    /// and the wait ends there rather than running out. One that does count it
    /// may still be asking its user, and is waited for.
    ///
    /// An answer that arrives after the wait has given up is not typing:
    /// `poll()` swallows it, and the DA1 answer with it.
    [[nodiscard]] HostClipboard hostClipboard(int waitMs);

    /// Throws away whatever has been typed but not yet read.
    ///
    /// For the one case where the application was busy rather than waiting: keys
    /// pressed while the areas were being rescanned were aimed at a list that
    /// was being rebuilt underneath them, and acting on them once it comes back
    /// would open an area nobody chose.
    ///
    /// **Never `flushinp()`.** The bytes are read through ncurses and dropped
    /// afterwards, so that a mouse release among them still reaches the state
    /// ncurses keeps about which buttons are down; thrown away unparsed, it
    /// leaves the button held as far as ncurses knows, and the next press comes
    /// back as a movement nothing acts on.
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

    /// Reads one code from the keyboard, waiting up to `waitMs` — or nothing
    /// where none came. A resize and a mouse report are dealt with on the way
    /// and never come back from here; a named key comes back as its code with
    /// `named` set.
    [[nodiscard]] std::optional<wint_t> readRaw(int waitMs, bool& named);

    /// The rest of an OSC once its `ESC ]` has been read, up to BEL or ST, or
    /// nothing where it broke off.
    [[nodiscard]] std::optional<std::string> readOsc();

    /// The rest of a CSI once its `ESC [` has been read, up to its final byte.
    [[nodiscard]] std::string readCsi();

    /// Swallows what is left of a reply once its ESC has been read — an OSC to
    /// its end, a CSI to its final byte — and says whether there was one.
    /// Anything else read is handed back to ncurses.
    bool swallowReply();

    /// Takes the other half of a CR LF (or LF CR) pair off the input where it
    /// is already waiting behind the half just read, so that the pair is one
    /// line break and not two. It is how text pasted by the console itself
    /// arrives — conhost types a paste out with the line endings Windows text
    /// carries — and an Enter pressed by hand is a single code, which this
    /// leaves alone. Anything else waiting is handed back to ncurses.
    void swallowLineEndPartner(wint_t first);

    /// Notes a mouse event's effect on whether the left button is held, and
    /// answers whether it is worth passing on: a movement is only while the
    /// button is down, being a drag then and a hand wandering otherwise. A
    /// movement that is really a press after a release nobody reported is
    /// rewritten into the press it is.
    bool noteButton(Event& event);

    /// How long the pointer has to have been still for a movement that jumps to
    /// be taken for a press: longer than the reports of a drag ever stand apart,
    /// which is a few tens of milliseconds, and shorter than a hand takes to let
    /// go of the button and click somewhere else.
    static constexpr long long kLostReleaseGapMs = 250;

    /// Asks the terminal to report the pointer moving while a button is held —
    /// xterm's button-event tracking, mode 1002 — or stops it. ncurses asks for
    /// presses and releases alone, and a drag is what a selection is made with.
    /// Written straight to the terminal as `ModifiedKeyReporting` is, and taken
    /// back off before the terminal is handed to anything else.
    void setDragReporting(bool on);

    static constexpr const char* kDragOn = "\x1b[?1002h";
    static constexpr const char* kDragOff = "\x1b[?1002l";

    Screen screen_;
    /// Whether a clipboard answer, and the DA1 answer sent after it, may still
    /// be on their way from a question given up on — which `poll()` then takes
    /// off the input rather than passing on as Escape and a run of letters.
    bool oscPending_{false};
    bool da1Pending_{false};
    bool running_{true};
    bool dragReporting_{false};
    bool leftHeld_{false};
    /// Where and when the last mouse report was, which is what tells a press
    /// reported as a movement from the movement of a drag.
    std::chrono::steady_clock::time_point lastMouseAt_{};
    int lastMouseX_{0};
    int lastMouseY_{0};
    bool cursorVisible_{false};
    int cursorX_{0};
    int cursorY_{0};
};

}  // namespace amberedit::ui::term
