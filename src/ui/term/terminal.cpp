#include "ui/term/terminal.hpp"

#include <termios.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

#include "ui/term/color.hpp"
#include "ui/term/ncurses.hpp"
#include "ui/term/utf8.hpp"

namespace amberedit::ui::term {
namespace {

/// TEMPORARY, for tracking down a mouse that works in every terminal but one:
/// a line per thing the terminal sent and per thing ncurses made of it. Writes
/// nothing unless AMBEREDIT_MOUSE_LOG names a file.
void trace(const std::string& line) {
    static std::FILE* log = []() -> std::FILE* {
        const char* path = std::getenv("AMBEREDIT_MOUSE_LOG");
        return path != nullptr ? std::fopen(path, "w") : nullptr;
    }();
    if (log == nullptr) return;
    std::fputs(line.c_str(), log);
    std::fputc('\n', log);
    std::fflush(log);
}

std::string hex(unsigned long value) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "0x%lx", value);
    return buffer.data();
}

/// Custom key codes are handed out from just above the range terminfo uses, so
/// that they cannot collide with a key the terminal description already names.
int nextKeyCode = KEY_MAX + 1;

/// What each sequence registered with ncurses means. `define_key` gets us as far
/// as a distinct code per sequence; this says what that code stands for.
std::unordered_map<int, Event> customKeys;

void registerKey(const std::string& sequence, const Event& event) {
    const int code = nextKeyCode++;
    if (define_key(sequence.c_str(), code) != OK) {
        --nextKeyCode;
        return;
    }
    customKeys.emplace(code, event);
}

/// The escape sequences that carry a modifier, taught to ncurses one by one.
///
/// terminfo describes the keys a terminal has had since the 1980s and nothing
/// since, so a modified key is not in it: Ctrl-Q and Alt-F arrive as sequences
/// ncurses would otherwise hand over one byte at a time — and an Alt chord whose
/// leading ESC came through on its own would read as Escape and close the screen
/// the user was typing in. Naming them here is ncurses' own way of extending the
/// description, and it puts them through the same matcher as everything else,
/// with no timing left for this code to get wrong.
void registerModifiedKeys(const std::string& altLetters, bool altBackspace) {
    // The disambiguated Escape. With the kitty protocol on, a bare Escape is
    // reported like this precisely so it cannot be read as the start of
    // something longer — which is what makes pressing Esc reliable at last.
    registerKey("\x1b[27u", Event::Escape);

    for (char letter = 'a'; letter <= 'z'; ++letter) {
        const std::string code = std::to_string(static_cast<int>(letter));
        const std::string text(1, letter);
        // kitty's CSI u, with 5 for Ctrl and 3 for Alt. Unambiguous, so every
        // letter is registered whether anything binds it today or not.
        registerKey("\x1b[" + code + ";5u", Event::Character(text, true, false, false));
        registerKey("\x1b[" + code + ";3u", Event::Character(text, false, true, false));
    }

    // Alt as a bare ESC in front of the letter — what it has always meant on a
    // terminal that knows no protocol. This form is ambiguous with pressing
    // Escape and then the letter, so only the letters actually bound are
    // claimed: every one registered here is a letter Escape can no longer be
    // followed by. Which they are is the layout's to say, and `KeyMap` hands
    // them over.
    for (const char letter : altLetters) {
        registerKey(std::string("\x1b") + letter,
                    Event::Character(std::string(1, letter), false, true, false));
    }

    // Alt with an arrow. Modifier 3 is xterm's; some terminals send 9 instead.
    const std::array<std::pair<char, Event::Name>, 4> arrows{{
        {'A', Event::Name::ArrowUp},
        {'B', Event::Name::ArrowDown},
        {'C', Event::Name::ArrowRight},
        {'D', Event::Name::ArrowLeft},
    }};
    for (const auto& [final, name] : arrows) {
        const Event alt = Event::Named(name, false, true, false);
        registerKey(std::string("\x1b[1;3") + final, alt);
        registerKey(std::string("\x1b[1;9") + final, alt);
    }

    // Alt with Backspace, the other way a word is taken out. Both protocols
    // spell it as they spell any modified key, and unambiguously, so those two
    // forms are registered whether anything binds the chord or not. The old
    // form — a bare ESC in front of the byte Backspace sends, either of the two
    // it may be — is ambiguous with Escape then Backspace, and so is claimed
    // only where the layout asks for it.
    const Event altBack = Event::Named(Event::Name::Backspace, false, true, false);
    registerKey("\x1b[127;3u", altBack);
    registerKey("\x1b[27;3;127~", altBack);
    if (altBackspace) {
        registerKey("\x1b\x7f", altBack);
        registerKey("\x1b\b", altBack);
    }

    // Shift+Space, which is a key the terminal will not tell anyone about unless
    // asked: kitty spells it one way, xterm's modifyOtherKeys the other.
    const Event shiftSpace = Event::Character(" ", false, false, true);
    registerKey("\x1b[32;2u", shiftSpace);
    registerKey("\x1b[27;2;32~", shiftSpace);
}

/// Home and End, in the forms `khome` and `kend` do not name.
///
/// A terminfo entry names one sequence per key — the one the terminal it was
/// written for sends — and on this pair the terminals never agreed. xterm's
/// entry says `ESC O H` and `ESC O F`; a VT220, the Linux console, screen and
/// PuTTY send `ESC [ 1 ~` and `ESC [ 4 ~`; rxvt sends `ESC [ 7 ~` and
/// `ESC [ 8 ~`. Wherever TERM describes a different terminal from the one at
/// the other end — a login over ssh, a serial console, an emulator set to
/// VT220 — the key arrives as bytes ncurses cannot name and does nothing at
/// all. Which form is missing is not something this side can know, and none of
/// them means anything else anywhere, so every one is claimed rather than
/// guessed at: registering a form terminfo already resolves leaves it meaning
/// what it already meant. The two that a VT220 keyboard calls Find and Select
/// are read as Home and End, as every editor on that keyboard has read them,
/// and nothing here binds Find or Select.
void registerNavigationKeys() {
    for (const char* form : {"\x1b[1~", "\x1b[7~", "\x1b[H", "\x1bOH"}) {
        registerKey(form, Event::Home);
    }
    for (const char* form : {"\x1b[4~", "\x1b[8~", "\x1b[F", "\x1bOF"}) {
        registerKey(form, Event::End);
    }
}

/// Asks the terminal to report modified keys, and puts it back on the way out.
///
/// Terminals do not do this by default: Shift+Space arrives as a plain space,
/// indistinguishable from Space, unless the application opts in. Two protocols
/// cover the field between them, and a terminal that understands neither ignores
/// both sequences.
///
/// Written straight to stdout rather than through ncurses: this happens once at
/// each end of the run, when ncurses has nothing outstanding, and going through
/// its output would mean it believed it had drawn something it had not.
class ModifiedKeyReporting {
public:
    ModifiedKeyReporting() { write(kKittyPush, kXtermOn); }
    ~ModifiedKeyReporting() { write(kKittyPop, kXtermOff); }

    ModifiedKeyReporting(const ModifiedKeyReporting&) = delete;
    ModifiedKeyReporting& operator=(const ModifiedKeyReporting&) = delete;
    ModifiedKeyReporting(ModifiedKeyReporting&&) = delete;
    ModifiedKeyReporting& operator=(ModifiedKeyReporting&&) = delete;

private:
    static void write(const char* one, const char* two) {
        std::fputs(one, stdout);
        std::fputs(two, stdout);
        std::fflush(stdout);
    }

    /// kitty keyboard protocol, "disambiguate escape codes" only. Higher flags
    /// would rewrite Enter, Tab and Backspace too, which we have no use for.
    static constexpr const char* kKittyPush = "\x1b[>1u";
    static constexpr const char* kKittyPop = "\x1b[<u";
    /// xterm modifyOtherKeys level 1: keys that already mean something definite
    /// keep their usual bytes, so only the ambiguous ones change.
    static constexpr const char* kXtermOn = "\x1b[>4;1m";
    static constexpr const char* kXtermOff = "\x1b[>4;0m";
};

/// Stops the terminal from swallowing Ctrl-Q, and puts it back on the way out.
///
/// Ctrl-S and Ctrl-Q are XOFF and XON: by default the line discipline consumes
/// them to pause and resume output, so Ctrl-Q never reaches an application.
/// ncurses' raw() turns off signals and line editing but leaves flow control
/// alone, so it is turned off here — after ncurses has taken its own snapshot,
/// so that endwin() still puts back what it found.
class FlowControlOff {
public:
    FlowControlOff() {
        if (tcgetattr(STDIN_FILENO, &original_) != 0) return;
        restore_ = true;

        termios raw = original_;
        raw.c_iflag &= ~(static_cast<tcflag_t>(IXON) | static_cast<tcflag_t>(IXOFF));
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    ~FlowControlOff() {
        if (restore_) tcsetattr(STDIN_FILENO, TCSANOW, &original_);
    }

    FlowControlOff(const FlowControlOff&) = delete;
    FlowControlOff& operator=(const FlowControlOff&) = delete;
    FlowControlOff(FlowControlOff&&) = delete;
    FlowControlOff& operator=(FlowControlOff&&) = delete;

private:
    termios original_{};
    bool restore_{false};
};

/// Lives as long as the Terminal does. Held here rather than as members so that
/// terminal.hpp does not have to name termios.
std::unique_ptr<FlowControlOff> flowControl;
std::unique_ptr<ModifiedKeyReporting> keyReporting;

attr_t attributesOf(uint8_t attrs) {
    attr_t out = A_NORMAL;
    if ((attrs & kBold) != 0) out |= A_BOLD;
    if ((attrs & kInverted) != 0) out |= A_REVERSE;
    if ((attrs & kUnderlined) != 0) out |= A_UNDERLINE;
#ifdef A_ITALIC
    if ((attrs & kItalic) != 0) out |= A_ITALIC;
#endif
    return out;
}

/// Maps the keys terminfo does describe. Everything modified went through
/// `registerModifiedKeys()` and comes back as a custom code instead.
Event namedKey(int code) {
    switch (code) {
        case KEY_UP: return Event::ArrowUp;
        case KEY_DOWN: return Event::ArrowDown;
        case KEY_LEFT: return Event::ArrowLeft;
        case KEY_RIGHT: return Event::ArrowRight;
        case KEY_HOME: return Event::Home;
        case KEY_END: return Event::End;
        case KEY_NPAGE: return Event::PageDown;
        case KEY_PPAGE: return Event::PageUp;
        case KEY_BACKSPACE: return Event::Backspace;
        case KEY_DC: return Event::Delete;
        case KEY_BTAB: return Event::TabReverse;
        case KEY_ENTER: return Event::Return;
        default: break;
    }
    if (code >= KEY_F(1) && code <= KEY_F(12)) {
        const auto offset = static_cast<uint8_t>(code - KEY_F(1));
        return Event::Named(
            static_cast<Event::Name>(static_cast<uint8_t>(Event::Name::F1) + offset));
    }
    return {};
}

/// Everything AmberEdit acts on, and deliberately nothing else — see the note on
/// the `mousemask` call for why the click bits have to stay out of it.
mmask_t mouseEvents() {
    mmask_t mask = static_cast<mmask_t>(BUTTON1_PRESSED) |
                   static_cast<mmask_t>(BUTTON1_RELEASED) |
                   static_cast<mmask_t>(BUTTON4_PRESSED);
#ifdef BUTTON5_PRESSED
    // Absent where ncurses was built with the older, narrower button mask, which
    // has no room for a fifth button.
    mask |= static_cast<mmask_t>(BUTTON5_PRESSED);
#endif
    return mask;
}

/// A press and its release that ncurses resolved into a click on the way in.
/// Nothing asks for these — see the `mousemask` note — but a curses that hands one
/// over anyway is read as the press it was made from rather than dropped.
mmask_t clickEvents() {
    return static_cast<mmask_t>(BUTTON1_CLICKED) |
           static_cast<mmask_t>(BUTTON1_DOUBLE_CLICKED) |
           static_cast<mmask_t>(BUTTON1_TRIPLE_CLICKED);
}

/// What a single report from ncurses means, or nothing at all if it is a button
/// AmberEdit does not bind.
Event reportEvent(const MEVENT& report) {
    MouseEvent mouse;
    mouse.x = report.x;
    mouse.y = report.y;
    mouse.motion = MouseEvent::Motion::Pressed;

    if ((report.bstate & (static_cast<mmask_t>(BUTTON1_PRESSED) | clickEvents())) != 0) {
        mouse.button = MouseEvent::Button::Left;
    } else if ((report.bstate & static_cast<mmask_t>(BUTTON4_PRESSED)) != 0) {
        mouse.button = MouseEvent::Button::WheelUp;
#ifdef BUTTON5_PRESSED
    } else if ((report.bstate & static_cast<mmask_t>(BUTTON5_PRESSED)) != 0) {
        mouse.button = MouseEvent::Button::WheelDown;
#endif
    } else if ((report.bstate & static_cast<mmask_t>(BUTTON1_RELEASED)) != 0) {
        mouse.button = MouseEvent::Button::Left;
        mouse.motion = MouseEvent::Motion::Released;
    } else {
        // A button AmberEdit does not bind, or a bare movement report. Swallowed
        // rather than passed on as a nameless click.
        return {};
    }
    return Event::Mouse(mouse);
}

/// The reports behind one KEY_MOUSE, reduced to the one event worth acting on.
///
/// A single KEY_MOUSE can stand for more than one report. A finger is why: a tap
/// sends the press and the release together, with nothing between them, and ncurses
/// gathers such a run and announces it once. `getmouse` then hands the run back
/// newest first — the release, with the press still queued behind it — so asking
/// only once means acting on the release and dropping the press on the floor. That
/// is exactly what happened under Termux on Android: taps did nothing while the
/// wheel, which sends one report and no release, kept scrolling.
///
/// So the queue is drained, and the press is picked out of what it held. A mouse
/// on a desktop terminal is unaffected: it holds the button long enough that the
/// press and the release are two runs, and each drain finds one report.
Event mouseEvent() {
    Event press{};
    Event other{};

    MEVENT report;
    while (getmouse(&report) == OK) {
        trace("mouse bstate=" + hex(static_cast<unsigned long>(report.bstate)) +
              " x=" + std::to_string(report.x) + " y=" + std::to_string(report.y));

        const Event event = reportEvent(report);
        if (event.kind() == Event::Kind::None) continue;

        if (event.mouse().motion == MouseEvent::Motion::Pressed) {
            if (press.kind() == Event::Kind::None) press = event;
        } else if (other.kind() == Event::Kind::None) {
            other = event;
        }
    }

    // The release is passed on when it is all there was, which is what a mouse
    // sends as the button comes back up. Nothing binds it, and nothing should:
    // whatever the press did — opened a screen, closed a dialog — has already
    // happened, and acting again would act on what has since moved underneath.
    return press.kind() != Event::Kind::None ? press : other;
}

}  // namespace

Terminal::Terminal(std::string altLetters, bool altBackspace) : screen_(0, 0) {
    // Before ncurses starts: it reads the locale as it initialises, and what it
    // finds there decides the encoding everything is written out in.
    ensureUtf8Locale();

    initscr();
    raw();      // no line editing, and no signals — Ctrl-C is ours to handle
    noecho();   // nothing is echoed that this code did not draw
    nonl();     // Enter stays a carriage return rather than becoming a newline
    keypad(stdscr, TRUE);
    set_escdelay(25);
    curs_set(0);

    // Only the events acted on, and pointedly not ALL_MOUSE_EVENTS: that mask also
    // asks for the click bits, and asking for those lets ncurses replace a press
    // and the release behind it with a single BUTTON1_CLICKED — a state nothing
    // here binds, which would leave a tap looking like a button AmberEdit has no
    // name for. The interval is zeroed for the same reason from the other side: it
    // is how long ncurses waits to see whether a second click follows, and waiting
    // both delays the press and gives it something to glue the press to.
    const mmask_t granted = mousemask(mouseEvents(), nullptr);
    mouseinterval(0);

    const char* term = std::getenv("TERM");
    trace(std::string("TERM=") + (term != nullptr ? term : "(unset)") +
          " curses=" + curses_version() +
          " mouse_version=" + std::to_string(NCURSES_MOUSE_VERSION) +
          " asked=" + hex(static_cast<unsigned long>(mouseEvents())) +
          " granted=" + hex(static_cast<unsigned long>(granted)) +
          " has_mouse=" + (has_mouse() ? "yes" : "no"));

    initColors();

    flowControl = std::make_unique<FlowControlOff>();
    keyReporting = std::make_unique<ModifiedKeyReporting>();
    registerModifiedKeys(altLetters, altBackspace);
    registerNavigationKeys();

    screen_.resize(COLS, LINES);
}

Terminal::~Terminal() {
    // In the reverse order they were put on, and before the screen is given
    // back: the terminal has to be told to stop reporting modified keys while it
    // is still the one being talked to.
    keyReporting.reset();
    flowControl.reset();
    curs_set(1);
    endwin();
}

int Terminal::width() const { return screen_.width(); }
int Terminal::height() const { return screen_.height(); }

const std::string& Terminal::codeset() const { return ensureUtf8Locale(); }

void Terminal::showCursor(int x, int y) {
    cursorVisible_ = true;
    cursorX_ = x;
    cursorY_ = y;
}

void Terminal::hideCursor() { cursorVisible_ = false; }

void Terminal::syncSize() {
    if (screen_.width() == COLS && screen_.height() == LINES) return;
    screen_.resize(COLS, LINES);
    // What ncurses believes is on the screen no longer describes a screen of
    // this size, so it is told to forget all of it rather than work out a
    // difference against the wrong shape.
    wclear(stdscr);
}

void Terminal::draw(const Element& document) {
    // A safety net. The size is normally settled as the resize is read, but a
    // caller that drew before ever polling would otherwise lay out against
    // whatever the terminal was when it started.
    syncSize();

    render(screen_, document);

    for (int y = 0; y < screen_.height(); ++y) {
        for (int x = 0; x < screen_.width(); ++x) {
            const Cell& cell = screen_.at(x, y);
            // The second column of a double-width glyph: its neighbour already
            // covered it, and writing here would cut the glyph in half.
            if (cell.glyph.empty()) continue;

            std::array<wchar_t, CCHARW_MAX + 1> wide{};
            size_t pos = 0;
            size_t count = 0;
            while (pos < cell.glyph.size() && count < CCHARW_MAX) {
                wide[count++] = static_cast<wchar_t>(decodeUtf8(cell.glyph, pos));
            }
            wide[count] = L'\0';

            int pair = pairFor(cell.fg, cell.bg);
            cchar_t out;
            setcchar(&out, wide.data(), attributesOf(cell.attrs), 0, &pair);
            // The bottom-right cell cannot be written without the cursor having
            // to advance off the screen, so ncurses reports failure having drawn
            // it anyway. Nothing here can act on that, and everything else that
            // could fail here has already been clipped to the screen.
            mvwadd_wch(stdscr, y, x, &out);
        }
    }

    if (cursorVisible_) {
        curs_set(1);
        wmove(stdscr, cursorY_, cursorX_);
    } else {
        curs_set(0);
        // Parked where it can do no harm. Some terminals draw a block even when
        // told to hide it, and the bottom-right corner is where that shows least.
        wmove(stdscr, screen_.height() - 1, screen_.width() - 1);
    }

    wrefresh(stdscr);
}

void Terminal::handOver(const std::function<void()>& work) {
    // Out, in the destructor's order and for the destructor's reason: the
    // terminal has to be told to stop reporting modified keys, and to stop
    // reporting the mouse, while it is still the one being talked to. A shell
    // handed a terminal still sending those would see them as typing.
    //
    // def_prog_mode() then keeps whatever modes are left for reset_prog_mode()
    // to put back, since endwin() is about to hand the shell's own back. It is
    // asked after FlowControlOff has gone rather than before, so that what is
    // kept is what ncurses set up and flow control is turned off again on the
    // way in — by the same object, in the same order, as at the start of the run.
    keyReporting.reset();
    flowControl.reset();
    mousemask(0, nullptr);
    curs_set(1);
    def_prog_mode();
    endwin();

    work();

    // And back, in the constructor's. Nothing is initialised again — the colors
    // and the keys defined with define_key belong to the SCREEN, which endwin()
    // suspends rather than destroys.
    reset_prog_mode();
    mousemask(mouseEvents(), nullptr);
    mouseinterval(0);
    flowControl = std::make_unique<FlowControlOff>();
    keyReporting = std::make_unique<ModifiedKeyReporting>();
    curs_set(cursorVisible_ ? 1 : 0);

    // What ncurses believes is on the screen is whatever was there before the
    // shell wrote over it, so it is told to forget all of it rather than draw
    // the difference against something the user cannot see. The size is settled
    // at the same time: the window may have been resized while the shell had it,
    // and nothing was reading KEY_RESIZE to notice.
    wclear(stdscr);
    syncSize();
}

void Terminal::flushInput() { flushinp(); }

Event Terminal::poll() {
    while (true) {
        wint_t code = 0;
        const int status = wget_wch(stdscr, &code);

        if (status == ERR) continue;  // interrupted; ask again

        trace((status == KEY_CODE_YES ? "key code=" : "char code=") +
              std::to_string(static_cast<int>(code)));

        if (status == KEY_CODE_YES) {
            const auto key = static_cast<int>(code);
            if (key == KEY_RESIZE) {
                syncSize();
                return Event::Resize;
            }
            if (key == KEY_MOUSE) {
                const Event event = mouseEvent();
                if (event.kind() == Event::Kind::None) continue;
                return event;
            }
            if (const auto found = customKeys.find(key); found != customKeys.end()) {
                return found->second;
            }
            const Event event = namedKey(key);
            if (event.kind() == Event::Kind::None) continue;  // nothing binds it
            return event;
        }

        // An ordinary character, or one of the control bytes a terminal still
        // sends for the keys that predate terminfo.
        switch (code) {
            case 27: return Event::Escape;
            case 9: return Event::Tab;
            case 10:
            case 13: return Event::Return;
            case 8:
            case 127: return Event::Backspace;
            default: break;
        }

        // C0: Ctrl with a letter, which is what the byte has meant since ASCII.
        // Tab, Return and Backspace were taken above, being keys in their own
        // right rather than chords anyone presses as one.
        if (code < 27) {
            const auto letter = static_cast<char>('a' + static_cast<int>(code) - 1);
            return Event::Character(std::string(1, letter), true, false, false);
        }
        if (code < 32) continue;  // a control byte nothing binds

        return Event::Character(encodeUtf8(static_cast<char32_t>(code)));
    }
}

}  // namespace amberedit::ui::term
