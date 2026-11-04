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
#include <array>
#include <csignal>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
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
    return mode == "--keytest" || mode == "--keycheck";
}

namespace {

std::string joined(const std::vector<std::string>& words) {
    std::string out;
    for (const std::string& word : words) {
        if (!out.empty()) out += ' ';
        out += word;
    }
    return out;
}

std::string padded(std::string text, std::size_t width) {
    if (text.size() < width) text.append(width - text.size(), ' ');
    return text;
}

/// A line without the comment a `#` opens.
std::string uncommented(const std::string& line) {
    const std::size_t hash = line.find('#');
    return hash == std::string::npos ? line : line.substr(0, hash);
}

/// The `TERM:` a file's first line names, or nothing.
std::string termOf(const std::string& header) {
    const std::size_t at = header.find("TERM: ");
    if (at == std::string::npos) return {};
    const std::size_t from = at + 6;
    const std::size_t end = header.find(' ', from);
    return header.substr(from, end == std::string::npos ? std::string::npos : end - from);
}

/// Whether an Alt-letter came as what a macOS terminal makes of Option with a
/// letter where Option is not Meta: the character the keyboard layout puts
/// there, with no ESC anywhere in it. Which character is the layout's, so no
/// list of them could be kept; and bytes with no ESC are bytes nothing could
/// read as Alt.
bool optionComposed(const std::string& key, const std::string& bytes) {
    const bool altLetter = key.size() == 5 && key.compare(0, 4, "Alt-") == 0 &&
                           key[4] >= 'A' && key[4] <= 'Z';
    return altLetter && !bytes.empty() && bytes.find('\x1b') == std::string::npos;
}

}  // namespace

tl::expected<std::vector<Limit>, ErrorPtr> readLimits(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return failure("cannot read " + path);
    std::vector<Limit> limits;
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        const std::vector<std::string> words = config::text::tokenize(uncommented(line));
        if (words.empty()) continue;
        if (words.size() < 3) {
            return failure(path + ":" + std::to_string(number) +
                           ": a limit is a key, its bytes and what they are read as");
        }
        limits.push_back({words[0], unescapeBytes(words[1]),
                          joined({words.begin() + 2, words.end()})});
    }
    return limits;
}

#ifdef _WIN32

struct Checker::Impl {};

Checker::Checker(std::vector<Limit>) : impl_(std::make_unique<Impl>()) {}
Checker::~Checker() = default;

tl::expected<CheckResult, ErrorPtr> Checker::check(const std::string&) {
    CheckResult result;
    result.skipped = "the Windows console reports key codes, not bytes";
    return result;
}

int main(int, char*[]) {
    std::cerr << "keytest: the Windows console reports key codes, not bytes; "
                 "there is nothing here to take\n";
    return 2;
}

#else

namespace {

/// The letters both ends claim under Alt.
constexpr const char* kAllLetters = "abcdefghijklmnopqrstuvwxyz";

/// What the replay sends after a key's bytes to say they are over. A character
/// no key sends and nothing binds, so whatever comes back before it is what the
/// key was read as, and an unfinished sequence is cut off by it exactly as the
/// next key pressed would cut it off.
constexpr std::string_view kSentinel = "\xc2\xa4";  // ¤

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

/// The far end of the pty: a `Terminal`, and the name of each event it reads,
/// a line each on `out`. Every letter under Alt and Alt-Backspace are claimed,
/// as a layout binding all of them would claim them: the question is whether a
/// key can be told apart, and a layout leaving Alt-X unbound reads it as
/// Escape and an x on purpose. Never returns.
[[noreturn]] void replayChild(int out) {
    term::Terminal terminal(kAllLetters, true);
    const auto say = [out](const std::string& line) {
        const std::string text = line + "\n";
        (void)::write(out, text.data(), text.size());
    };
    say("ready");
    while (true) say(nameOf(terminal.poll()));
}

/// A `Terminal` on the far side of a pty, in a process of its own — forked and
/// not started again, so that the tests can use it as they are — reading what
/// is written into it and saying on a pipe what each event came to.
///
/// The pty's output — the frames ncurses draws there — is read all the while
/// and thrown away: a buffer left to fill blocks the child in the middle of a
/// write and the events stop.
class Replay {
public:
    Replay() = default;
    Replay(const Replay&) = delete;
    Replay& operator=(const Replay&) = delete;
    Replay(Replay&&) = delete;
    Replay& operator=(Replay&&) = delete;

    ~Replay() {
        if (pid_ > 0) {
            ::kill(pid_, SIGKILL);
            int status = 0;
            ::waitpid(pid_, &status, 0);
        }
        if (master_ >= 0) ::close(master_);
        if (names_ >= 0) ::close(names_);
    }

    /// Starts the far end under `term`, or says why it did not.
    bool start(const std::string& term, std::string& why) {
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
        std::fflush(nullptr);
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
            ::dup2(slave, STDERR_FILENO);
            ::close(slave);
            ::close(pipeFds[0]);
            ::close(master_);
            ::setenv("TERM", term.c_str(), 1);
            replayChild(pipeFds[1]);
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
                why = "no terminfo for TERM=" + term + " here";
                return false;
            }
        }
        why = "the replay did not answer";
        return false;
    }

    /// What `bytes` came to, the events blank separated: the same answer for
    /// the same bytes, so each run of them is asked once.
    std::string recognise(const std::string& bytes) {
        const auto known = answers_.find(bytes);
        if (known != answers_.end()) return known->second;
        return answers_[bytes] = ask(bytes);
    }

private:
    std::string ask(const std::string& bytes) {
        std::vector<std::string> seen;
        // The sentinel goes straight behind the bytes rather than after the
        // wait ncurses gives an ESC: a sequence the bytes only begin cannot be
        // finished by a character no sequence holds, so a lone ESC is read as
        // the Escape it is all the same, and a hundred files take seconds.
        put(bytes);
        put(std::string(kSentinel));
        const Clock::time_point began = Clock::now();
        while (msSince(began) < 3000) {
            pump(50);
            while (const auto line = takeLine()) {
                if (*line == kSentinel) return joined(seen);
                if (*line == "Resize") continue;
                seen.push_back(*line);
            }
        }
        return "(no answer)";
    }

    void put(const std::string& bytes) {
        std::size_t done = 0;
        while (done < bytes.size()) {
            const ssize_t n = ::write(master_, bytes.data() + done, bytes.size() - done);
            if (n <= 0) return;
            done += static_cast<std::size_t>(n);
        }
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
    std::map<std::string, std::string> answers_;
};

}  // namespace

struct Checker::Impl {
    std::vector<Limit> limits;
    /// The replay per TERM, or why there is none.
    std::map<std::string, std::unique_ptr<Replay>> replays;
    std::map<std::string, std::string> unplayable;

    Replay* replayFor(const std::string& term, std::string& why) {
        if (const auto found = replays.find(term); found != replays.end()) {
            return found->second.get();
        }
        if (const auto found = unplayable.find(term); found != unplayable.end()) {
            why = found->second;
            return nullptr;
        }
        auto replay = std::make_unique<Replay>();
        if (!replay->start(term, why)) {
            unplayable[term] = why;
            return nullptr;
        }
        return (replays[term] = std::move(replay)).get();
    }

    [[nodiscard]] bool excused(const Mismatch& mismatch) const {
        return std::any_of(limits.begin(), limits.end(), [&](const Limit& limit) {
            return limit.key == mismatch.key && limit.bytes == mismatch.bytes &&
                   limit.readAs == mismatch.readAs;
        });
    }
};

Checker::Checker(std::vector<Limit> limits) : impl_(std::make_unique<Impl>()) {
    impl_->limits = std::move(limits);
}

Checker::~Checker() = default;

tl::expected<CheckResult, ErrorPtr> Checker::check(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return failure("cannot read " + path);

    CheckResult result;
    std::vector<std::pair<std::string, std::string>> taken;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind('#', 0) == 0) {
            if (result.term.empty()) result.term = termOf(line);
            continue;
        }
        const std::vector<std::string> words = config::text::tokenize(uncommented(line));
        if (words.size() < 2 || words[1] == "-") continue;
        taken.emplace_back(words[0], unescapeBytes(words[1]));
    }
    if (result.term.empty()) return failure(path + ": names no TERM");

    Replay* replay = impl_->replayFor(result.term, result.skipped);
    if (replay == nullptr) return result;
    // What each key was taken as in this file: a key read as another one the
    // terminal sent the very same bytes for is a key the terminal did not tell
    // apart, and nothing reading the bytes could do better.
    std::map<std::string, std::string> bytesOf(taken.begin(), taken.end());
    for (const auto& [key, bytes] : taken) {
        ++result.keys;
        Mismatch mismatch{key, bytes, replay->recognise(bytes)};
        if (mismatch.readAs == key) continue;
        const auto twin = bytesOf.find(mismatch.readAs);
        if ((twin != bytesOf.end() && twin->second == bytes) ||
            optionComposed(key, bytes) || impl_->excused(mismatch)) {
            ++result.excused;
        } else {
            result.wrong.push_back(std::move(mismatch));
        }
    }
    return result;
}

namespace {

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

int captureMain(const CaptureOptions& options) {
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

    // Started before this process takes the terminal over: the far end is a
    // fork, and a fork of a process with ncurses up would inherit it.
    Replay replay;
    std::string replayWhy;
    const bool replaying = replay.start(term, replayWhy);

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

        // A window that has just been opened may not have the keyboard yet, and
        // a key that goes astray puts every answer after it one key off. So a
        // driver is asked to press Space until something arrives, and the first
        // key is asked for only then.
        if (sync != nullptr) {
            terminal.draw(term::text("AmberEdit keytest: waiting for the driver"));
            for (int tries = 0; tries < 30; ++tries) {
                std::fputs("warmup\n", sync);
                std::fflush(sync);
                if (!readBurst(STDIN_FILENO, 1000, 300).empty()) break;
            }
            (void)readBurst(STDIN_FILENO, 500, 300);
        }

        for (std::size_t i = 0; i < list.size(); ++i) {
            const std::string name = nameOf(list[i]);
            using term::text;
            term::Elements rows;
            std::string heading = "AmberEdit keytest   ";
            heading.append(label).append("   TERM=").append(term);
            heading.append("   protocol: ").append(options.protocolText);
            rows.push_back(text(heading));
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
                const std::string seen = replay.recognise(bytes);
                const bool same = seen == name;
                if (!same) ++mismatches;
                comment = same ? "# ok" : "# seen: " + (seen.empty() ? "(nothing)" : seen);
            }
            out << padded(name, 16) << padded(escaped, 24) << comment << "\n";
            out.flush();
            last = name;
            last.append("  ").append(escaped).append("  ").append(comment);

            if (sync != nullptr) {
                std::fprintf(sync, "done %s %s\n", name.c_str(), escaped.c_str());
                std::fflush(sync);
                // Nothing arrived: the terminal kept the key for itself — and may
                // have opened something that holds the keyboard — or the key is a
                // dead one waiting for a letter to put an accent on. A driver
                // answers `flush` by putting it away, an Escape and a Space, and
                // what that sends is not the next key.
                if (bytes.empty()) {
                    std::fputs("flush\n", sync);
                    std::fflush(sync);
                    (void)readBurst(STDIN_FILENO, 1500, 300);
                }
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

int checkMain(int argc, char* argv[]) {
    std::vector<Limit> limits;
    std::vector<std::string> files;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--limits" && i + 1 < argc) {
            auto read = readLimits(argv[++i]);
            if (!read) {
                std::cerr << "keycheck: " << read.error()->message() << "\n";
                return 2;
            }
            limits = std::move(*read);
        } else {
            files.push_back(arg);
        }
    }
    if (files.empty()) {
        std::cerr << "keycheck: name a file taken by --keytest\n";
        return 2;
    }

    Checker checker(std::move(limits));
    int worst = 0;
    for (const std::string& path : files) {
        auto checked = checker.check(path);
        if (!checked) {
            std::cout << checked.error()->message() << "\n";
            worst = 2;
            continue;
        }
        const CheckResult& result = *checked;
        if (!result.skipped.empty()) {
            std::cout << path << ": skipped, " << result.skipped << "\n";
            continue;
        }
        for (const Mismatch& mismatch : result.wrong) {
            std::cout << path << ": " << padded(mismatch.key, 16)
                      << padded(escapeBytes(mismatch.bytes), 24) << "seen: "
                      << (mismatch.readAs.empty() ? "(nothing)" : mismatch.readAs)
                      << "\n";
        }
        std::cout << path << ": " << result.keys << " keys, " << result.wrong.size()
                  << " read as something else";
        if (result.excused != 0) std::cout << ", " << result.excused << " by a limit";
        std::cout << "\n";
        if (!result.wrong.empty()) worst = std::max(worst, 1);
    }
    return worst;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (std::string_view(argv[1]) == "--keycheck") return checkMain(argc, argv);

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
    return captureMain(options);
}

#endif

}  // namespace amberedit::ui::keytest
