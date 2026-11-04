#include "ui/keytest.hpp"

#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <csignal>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/text_util.hpp"
#include "ui/keys.hpp"
#include "ui/term/element.hpp"
#include "ui/term/terminal.hpp"
#include "version.hpp"

namespace amberedit::ui::keytest {
namespace {

using term::Event;

/// The keys with a name of their own, in the order a run asks for them bare.
constexpr Event::Name kNamedKeys[] = {
    Event::Name::ArrowUp,  Event::Name::ArrowDown, Event::Name::ArrowLeft,
    Event::Name::ArrowRight, Event::Name::Home,    Event::Name::End,
    Event::Name::PageUp,   Event::Name::PageDown,  Event::Name::Insert,
    Event::Name::Delete,   Event::Name::Backspace, Event::Name::Tab,
    Event::Name::Return,   Event::Name::Escape,    Event::Name::F1,
    Event::Name::F2,       Event::Name::F3,        Event::Name::F4,
    Event::Name::F5,       Event::Name::F6,        Event::Name::F7,
    Event::Name::F8,       Event::Name::F9,        Event::Name::F10,
    Event::Name::F11,      Event::Name::F12,
};

/// What the replay sends after a key's bytes to say they are over. A character
/// no key sends and nothing binds, so whatever comes back before it is what the
/// key was read as, and an unfinished sequence is cut off by it exactly as the
/// next key pressed would cut it off.
/// The letters both ends claim under Alt.
constexpr const char* kAllLetters = "abcdefghijklmnopqrstuvwxyz";

constexpr std::string_view kSentinel = "\xc2\xa4";  // ¤

void add(std::vector<Event>& keys, const Event& key) {
    const std::string name = nameOf(key);
    for (const Event& known : keys) {
        if (nameOf(known) == name) return;
    }
    keys.push_back(key);
}

bool isHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/// The words of `modifier_key_reporting`, or nothing where they are not.
bool parseProtocols(const std::string& text, std::vector<term::KeyProtocol>& out) {
    out.clear();
    for (const std::string& word : config::text::tokenize(text)) {
        const std::string value = config::text::toLower(word);
        term::KeyProtocol protocol{};
        if (value == "xterm") {
            protocol = term::KeyProtocol::Xterm;
        } else if (value == "kitty") {
            protocol = term::KeyProtocol::Kitty;
        } else {
            return false;
        }
        if (std::find(out.begin(), out.end(), protocol) != out.end()) return false;
        out.push_back(protocol);
    }
    return !out.empty();
}

}  // namespace

std::string nameOf(const Event& key) {
    switch (key.kind()) {
        case Event::Kind::None: return "None";
        case Event::Kind::Resize: return "Resize";
        case Event::Kind::Mouse: return "Mouse";
        default: break;
    }
    std::string prefix;
    if (key.ctrl()) prefix += "Ctrl-";
    if (key.alt()) prefix += "Alt-";
    if (key.kind() == Event::Kind::Key && key.name() == Event::Name::TabReverse) {
        return prefix + "Shift-Tab";
    }
    if (key.is_character() && key.input() == " ") {
        return prefix + (key.shift() ? "Shift-" : "") + "Space";
    }
    return spellingOf(key);
}

std::vector<Event> keys() {
    std::vector<Event> out;
    for (const Event::Name name : kNamedKeys) add(out, Event::Named(name));
    add(out, Event::TabReverse);
    add(out, Event::Character(" "));
    add(out, Event::Character(" ", false, false, true));

    // Each named key under whichever single modifier a layout may bind it with,
    // and Ctrl with Home and End, which no layout may but the screens answer.
    for (const Event::Name name : kNamedKeys) {
        const Event chords[] = {Event::Named(name, true, false, false),
                                Event::Named(name, false, true, false),
                                Event::Named(name, false, false, true)};
        for (const Event& chord : chords) {
            const bool ctrlEnd =
                chord.ctrl() && (name == Event::Name::Home || name == Event::Name::End);
            if (ctrlEnd || keyNamed(nameOf(chord))) add(out, chord);
        }
    }

    for (char c = 'a'; c <= 'z'; ++c) add(out, Event::Character(std::string(1, c), true));
    for (char c = 'a'; c <= 'z'; ++c) {
        add(out, Event::Character(std::string(1, c), false, true));
    }

    const KeyMap defaults = KeyMap::defaults();
    for (const Commands::Info& info : Commands::all()) {
        for (const Event& key : defaults.keysOf(info.command)) add(out, key);
    }
    return out;
}

std::string escapeBytes(const std::string& bytes) {
    std::string out;
    for (const char c : bytes) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte == 0x1b) {
            out += "\\e";
        } else if (byte > ' ' && byte < 0x7f && byte != '\\' && byte != '-') {
            out += c;
        } else {
            std::array<char, 8> hex{};
            std::snprintf(hex.data(), hex.size(), "\\x%02x", byte);
            out += hex.data();
        }
    }
    return out;
}

std::string unescapeBytes(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) {
            out += text[i];
            continue;
        }
        const char kind = text[i + 1];
        if (kind == 'e') {
            out += '\x1b';
            ++i;
        } else if (kind == 'x' && i + 3 < text.size() && isHex(text[i + 2]) &&
                   isHex(text[i + 3])) {
            out += static_cast<char>(std::stoi(text.substr(i + 2, 2), nullptr, 16));
            i += 3;
        } else if (kind == '\\') {
            out += '\\';
            ++i;
        } else {
            out += text[i];
        }
    }
    return out;
}

bool wanted(int argc, char* argv[]) {
    if (argc < 2) return false;
    const std::string_view mode = argv[1];
    return mode == "--keytest" || mode == "--keycheck" || mode == "--keytest-replay";
}

#ifdef _WIN32

int main(int, char*[]) {
    std::cerr << "keytest: the Windows console reports key codes, not bytes; "
                 "there is nothing here to take\n";
    return 2;
}

#else

namespace {

using Clock = std::chrono::steady_clock;

int msSince(Clock::time_point start) {
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start)
            .count());
}

/// Whatever arrives on `fd` within `firstMs`, and everything after it that
/// follows with gaps no longer than `gapMs` — one key's worth, since a terminal
/// writes a key's sequence in one go and a person does not press two keys
/// sixty milliseconds apart.
std::string readBurst(int fd, int firstMs, int gapMs) {
    std::string bytes;
    int wait = firstMs;
    while (true) {
        pollfd ready{fd, POLLIN, 0};
        const int got = ::poll(&ready, 1, wait);
        if (got <= 0) break;
        std::array<char, 256> buffer{};
        const ssize_t n = ::read(fd, buffer.data(), buffer.size());
        if (n <= 0) break;
        bytes.append(buffer.data(), static_cast<std::size_t>(n));
        wait = gapMs;
    }
    return bytes;
}

/// An AmberEdit `Terminal` on the far side of a pty, reading what is written
/// into it and saying on a pipe what each event came to.
///
/// The pty's output — the frames ncurses draws there — is read all the while
/// and thrown away: a buffer left to fill blocks the child in the middle of a
/// write and the events stop.
class Replay {
public:
    Replay() = default;
    Replay(const Replay&) = delete;
    Replay& operator=(const Replay&) = delete;

    ~Replay() {
        if (pid_ > 0) {
            ::kill(pid_, SIGKILL);
            int status = 0;
            ::waitpid(pid_, &status, 0);
        }
        if (master_ >= 0) ::close(master_);
        if (names_ >= 0) ::close(names_);
    }

    /// Starts `self --keytest-replay` under `term`, or says why it did not.
    bool start(const std::string& self, const std::string& term, std::string& why) {
        master_ = ::posix_openpt(O_RDWR | O_NOCTTY);
        if (master_ < 0 || ::grantpt(master_) != 0 || ::unlockpt(master_) != 0) {
            why = std::string("cannot open a pty: ") + std::strerror(errno);
            return false;
        }
        const char* slaveName = ::ptsname(master_);
        if (slaveName == nullptr) {
            why = "cannot name the pty";
            return false;
        }
        const std::string slavePath = slaveName;
        int pipeFds[2];
        if (::pipe(pipeFds) != 0) {
            why = std::string("cannot open a pipe: ") + std::strerror(errno);
            return false;
        }
        pid_ = ::fork();
        if (pid_ < 0) {
            why = std::string("cannot fork: ") + std::strerror(errno);
            return false;
        }
        if (pid_ == 0) {
            ::setsid();
            const int slave = ::open(slavePath.c_str(), O_RDWR);
            if (slave < 0) ::_exit(127);
            ::ioctl(slave, TIOCSCTTY, 0);
            winsize size{};
            size.ws_row = 24;
            size.ws_col = 80;
            ::ioctl(slave, TIOCSWINSZ, &size);
            ::dup2(slave, STDIN_FILENO);
            ::dup2(slave, STDOUT_FILENO);
            ::dup2(pipeFds[1], STDERR_FILENO);
            ::close(slave);
            ::close(pipeFds[0]);
            ::close(pipeFds[1]);
            ::close(master_);
            ::setenv("TERM", term.c_str(), 1);
            ::execl(self.c_str(), self.c_str(), "--keytest-replay",
                    static_cast<char*>(nullptr));
            ::_exit(127);
        }
        ::close(pipeFds[1]);
        names_ = pipeFds[0];

        const Clock::time_point began = Clock::now();
        while (msSince(began) < 5000) {
            pump(50);
            if (const auto line = takeLine()) {
                if (*line == "ready") return true;
            }
            int status = 0;
            if (::waitpid(pid_, &status, WNOHANG) == pid_) {
                pid_ = -1;
                why = "the replay did not start (is terminfo for TERM=" + term +
                      " installed?)";
                return false;
            }
        }
        why = "the replay did not answer";
        return false;
    }

    /// What `bytes` came to, an event at a time.
    std::vector<std::string> recognise(const std::string& bytes) {
        std::vector<std::string> seen;
        put(bytes);
        // Longer than ncurses waits to see whether an ESC begins something, so
        // that a lone one is read as the Escape it is before the sentinel lands.
        pumpFor(80);
        put(std::string(kSentinel));
        const Clock::time_point began = Clock::now();
        while (msSince(began) < 3000) {
            pump(50);
            while (const auto line = takeLine()) {
                if (*line == kSentinel) return seen;
                if (*line == "Resize") continue;
                seen.push_back(*line);
            }
        }
        seen.emplace_back("(no answer)");
        return seen;
    }

private:
    void put(const std::string& bytes) {
        std::size_t done = 0;
        while (done < bytes.size()) {
            const ssize_t n = ::write(master_, bytes.data() + done, bytes.size() - done);
            if (n <= 0) return;
            done += static_cast<std::size_t>(n);
        }
    }

    void pumpFor(int ms) {
        const Clock::time_point began = Clock::now();
        while (msSince(began) < ms) pump(ms - msSince(began));
    }

    /// Reads whatever is waiting on either descriptor, for up to `ms`.
    void pump(int ms) {
        pollfd fds[2] = {{master_, POLLIN, 0}, {names_, POLLIN, 0}};
        if (::poll(fds, 2, std::max(ms, 0)) <= 0) return;
        std::array<char, 4096> buffer{};
        if ((fds[0].revents & (POLLIN | POLLHUP)) != 0) {
            (void)::read(master_, buffer.data(), buffer.size());
        }
        if ((fds[1].revents & (POLLIN | POLLHUP)) != 0) {
            const ssize_t n = ::read(names_, buffer.data(), buffer.size());
            if (n > 0) namesText_.append(buffer.data(), static_cast<std::size_t>(n));
        }
    }

    std::optional<std::string> takeLine() {
        const std::size_t end = namesText_.find('\n');
        if (end == std::string::npos) return std::nullopt;
        std::string line = namesText_.substr(0, end);
        namesText_.erase(0, end + 1);
        return line;
    }

    pid_t pid_{-1};
    int master_{-1};
    int names_{-1};
    std::string namesText_;
};

std::string joined(const std::vector<std::string>& words) {
    std::string out;
    for (const std::string& word : words) {
        if (!out.empty()) out += ' ';
        out += word;
    }
    return out.empty() ? "(nothing)" : out;
}

std::string padded(std::string text, std::size_t width) {
    if (text.size() < width) text.append(width - text.size(), ' ');
    return text;
}

int replayMain() {
    // Every letter under Alt and Alt-Backspace, as a layout binding all of them
    // would claim them: the question is whether a key can be told apart, and a
    // layout that leaves Alt-X unbound reads it as Escape and an x on purpose.
    term::Terminal terminal(kAllLetters, true);
    std::fputs("ready\n", stderr);
    std::fflush(stderr);
    while (terminal.running()) {
        const Event event = terminal.poll();
        std::fprintf(stderr, "%s\n", nameOf(event).c_str());
        std::fflush(stderr);
    }
    return 0;
}

struct CaptureOptions {
    std::string protocolText{"xterm kitty"};
    std::vector<term::KeyProtocol> protocols{term::KeyProtocol::Xterm,
                                             term::KeyProtocol::Kitty};
    std::string label;
    std::string outPath;
    std::string syncPath;
    int waitMs{15000};
    std::vector<std::string> only;
};

int captureMain(const std::string& self, const CaptureOptions& options) {
    std::vector<Event> list;
    for (const Event& key : keys()) {
        const std::string name = nameOf(key);
        if (options.only.empty() ||
            std::find(options.only.begin(), options.only.end(), name) !=
                options.only.end()) {
            list.push_back(key);
        }
    }

    const char* termEnv = std::getenv("TERM");
    const std::string term = termEnv != nullptr ? termEnv : "";
    const std::string label = options.label.empty() ? "unnamed" : options.label;
    const std::string outPath =
        options.outPath.empty() ? "keytest.txt" : options.outPath;

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "keytest: cannot write " << outPath << "\n";
        return 1;
    }

    Replay replay;
    std::string replayWhy;
    const bool replaying = replay.start(self, term, replayWhy);

    std::FILE* sync = nullptr;
    if (!options.syncPath.empty()) {
        sync = std::fopen(options.syncPath.c_str(), "w");
        if (sync == nullptr) {
            std::cerr << "keytest: cannot open " << options.syncPath << "\n";
            return 1;
        }
    }

    out << "# terminal: " << label << "   TERM: " << term
        << "   protocol: " << options.protocolText << "\n";
    out << "# taken by: " << kProgramId << "\n";

    std::size_t mismatches = 0;
    {
        // Claimed as the replay claims them, so that the bytes taken are the
        // ones AmberEdit would be reading.
        term::Terminal terminal(kAllLetters, true, options.protocols);
        std::string last = "-";
        // Whatever the requests were answered with, or a key still held.
        (void)readBurst(STDIN_FILENO, 300, 100);

        for (std::size_t i = 0; i < list.size(); ++i) {
            const std::string name = nameOf(list[i]);
            using term::text;
            term::Elements rows;
            rows.push_back(text("AmberEdit keytest   " + label + "   TERM=" + term +
                                "   protocol: " + options.protocolText));
            rows.push_back(text(""));
            rows.push_back(term::bold(text("Press  " + name)));
            rows.push_back(text("key " + std::to_string(i + 1) + " of " +
                                std::to_string(list.size()) + "; nothing within " +
                                std::to_string(options.waitMs / 1000) +
                                " s skips it"));
            rows.push_back(text(""));
            rows.push_back(text("last:  " + last));
            if (!replaying) rows.push_back(text("not checked: " + replayWhy));
            terminal.draw(term::vbox(std::move(rows)));

            if (sync != nullptr) {
                std::fprintf(sync, "ready %s\n", name.c_str());
                std::fflush(sync);
            }
            const std::string bytes = readBurst(STDIN_FILENO, options.waitMs, 60);
            const std::string escaped = bytes.empty() ? "-" : escapeBytes(bytes);

            std::string comment;
            if (!bytes.empty() && replaying) {
                const std::vector<std::string> seen = replay.recognise(bytes);
                const bool same = seen.size() == 1 && seen[0] == name;
                if (!same) ++mismatches;
                comment = same ? "# ok" : "# seen: " + joined(seen);
            }
            out << padded(name, 16) << padded(escaped, 24) << comment << "\n";
            out.flush();
            last = name;
            last.append("  ").append(escaped).append("  ").append(comment);

            if (sync != nullptr) {
                std::fprintf(sync, "done %s %s\n", name.c_str(), escaped.c_str());
                std::fflush(sync);
                // A driver answers a key the terminal kept for itself by putting
                // away whatever it opened — an Escape, a click beside a popover —
                // and what that sends is not the next key.
                if (bytes.empty()) (void)readBurst(STDIN_FILENO, 700, 150);
            }
        }
    }
    if (sync != nullptr) std::fclose(sync);
    std::cout << outPath << ": " << list.size() << " keys";
    if (replaying) std::cout << ", " << mismatches << " read as something else";
    std::cout << "\n";
    if (!replaying) std::cerr << "keytest: not checked: " << replayWhy << "\n";
    return 0;
}

/// The `TERM:` a file's first line names, or nothing.
std::string termOf(const std::string& header) {
    const std::size_t at = header.find("TERM: ");
    if (at == std::string::npos) return {};
    const std::size_t from = at + 6;
    const std::size_t end = header.find(' ', from);
    return header.substr(from, end == std::string::npos ? std::string::npos : end - from);
}

int checkFile(const std::string& self, const std::string& path, std::ostream& report) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        report << path << ": cannot read\n";
        return 2;
    }
    std::string line;
    std::string term;
    std::vector<std::pair<std::string, std::string>> taken;
    while (std::getline(in, line)) {
        if (line.rfind('#', 0) == 0) {
            if (term.empty()) term = termOf(line);
            continue;
        }
        const std::vector<std::string> words = config::text::tokenize(line);
        if (words.size() < 2 || words[1] == "-") continue;
        taken.emplace_back(words[0], unescapeBytes(words[1]));
    }
    if (term.empty()) {
        report << path << ": names no TERM\n";
        return 2;
    }

    Replay replay;
    std::string why;
    if (!replay.start(self, term, why)) {
        report << path << ": skipped, " << why << "\n";
        return 0;
    }
    std::size_t wrong = 0;
    for (const auto& [name, bytes] : taken) {
        const std::vector<std::string> seen = replay.recognise(bytes);
        if (seen.size() == 1 && seen[0] == name) continue;
        ++wrong;
        report << path << ": " << padded(name, 16) << padded(escapeBytes(bytes), 24)
               << "seen: " << joined(seen) << "\n";
    }
    report << path << ": " << taken.size() << " keys, " << wrong
           << " read as something else\n";
    return wrong == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    // The replay is this same binary, started again: by the path the system
    // has for it where there is one, argv[0] being whatever PATH found.
    std::string self = argv[0];
    std::array<char, 4096> exe{};
    const ssize_t length = ::readlink("/proc/self/exe", exe.data(), exe.size() - 1);
    if (length > 0) self.assign(exe.data(), static_cast<std::size_t>(length));
    const std::string_view mode = argv[1];

    if (mode == "--keytest-replay") return replayMain();

    if (mode == "--keycheck") {
        if (argc < 3) {
            std::cerr << "keycheck: name a file taken by --keytest\n";
            return 2;
        }
        int worst = 0;
        for (int i = 2; i < argc; ++i) {
            worst = std::max(worst, checkFile(self, argv[i], std::cout));
        }
        return worst;
    }

    CaptureOptions options;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--out" && hasValue) {
            options.outPath = argv[++i];
        } else if (arg == "--label" && hasValue) {
            options.label = argv[++i];
        } else if (arg == "--sync" && hasValue) {
            options.syncPath = argv[++i];
        } else if (arg == "--wait" && hasValue) {
            options.waitMs = std::max(100, std::atoi(argv[++i]));
        } else if (arg == "--only" && hasValue) {
            std::string list = argv[++i];
            std::replace(list.begin(), list.end(), ',', ' ');
            options.only = config::text::tokenize(list);
        } else if (arg.rfind("--", 0) != 0) {
            if (!parseProtocols(arg, options.protocols)) {
                std::cerr << "keytest: '" << arg
                          << "' is not what modifier_key_reporting takes "
                             "(one or two of xterm | kitty)\n";
                return 2;
            }
            options.protocolText = arg;
        } else {
            std::cerr << "keytest: unknown option '" << arg << "'\n";
            return 2;
        }
    }
    return captureMain(self, options);
}

#endif

}  // namespace amberedit::ui::keytest
