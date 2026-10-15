#include "sys/process.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <pwd.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace amberedit::sys {
namespace {

/// Ctrl-C and Ctrl-\ put aside for as long as another program holds the
/// terminal, and given back however this leaves.
///
/// Whatever runs reaches this process as well, and the reader is not what the
/// user meant to interrupt. A program that takes the terminal's foreground group
/// as it starts answers for the keys after that; this covers the moment before
/// it has, and the programs that never do.
///
/// It is the parent that goes deaf and the child that has to hear again before
/// exec, since an ignored signal stays ignored across one. Windows has no
/// SIGQUIT and no fork, so there only the interrupt is put aside.
class InterruptsAside {
public:
    InterruptsAside() {
        interrupt_ = std::signal(SIGINT, SIG_IGN);
#ifdef SIGQUIT
        quit_ = std::signal(SIGQUIT, SIG_IGN);
#endif
    }
    ~InterruptsAside() { restore(); }

    InterruptsAside(const InterruptsAside&) = delete;
    InterruptsAside& operator=(const InterruptsAside&) = delete;
    InterruptsAside(InterruptsAside&&) = delete;
    InterruptsAside& operator=(InterruptsAside&&) = delete;

    void restore() const {
        if (interrupt_ != SIG_ERR) std::signal(SIGINT, interrupt_);
#ifdef SIGQUIT
        if (quit_ != SIG_ERR) std::signal(SIGQUIT, quit_);
#endif
    }

private:
    void (*interrupt_)(int){SIG_ERR};
#ifdef SIGQUIT
    void (*quit_)(int){SIG_ERR};
#endif
};

CommandResult failedToStart(std::string reason) {
    return {CommandStage::NotStarted, std::move(reason)};
}

#ifdef _WIN32

/// The system's words for the last failure, in the user's own language since
/// that is what FormatMessage answers in.
std::string lastErrorText() {
    const DWORD code = ::GetLastError();
    LPSTR text = nullptr;
    const DWORD length = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPSTR>(&text), 0, nullptr);
    if (length == 0 || text == nullptr) return "error " + std::to_string(code);

    std::string message(text, length);
    ::LocalFree(text);
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n' ||
                                message.back() == ' ' || message.back() == '.')) {
        message.pop_back();
    }
    return message;
}

/// One argument, quoted the way CommandLineToArgvW will take it apart again.
///
/// Windows hands a process its command line as a single string and leaves the
/// splitting to the program, so an argument holding a space — a path under
/// "Program Files", a subject line — has to be quoted here or it arrives as two.
/// The backslash rule is the awkward part: a run of backslashes is doubled only
/// where a quote follows it, because only there does it mean escaping.
void appendQuoted(std::string& line, const std::string& word) {
    if (!word.empty() && word.find_first_of(" \t\n\v\"") == std::string::npos) {
        line += word;
        return;
    }

    line += '"';
    for (size_t i = 0; i < word.size(); ++i) {
        size_t slashes = 0;
        while (i < word.size() && word[i] == '\\') {
            ++slashes;
            ++i;
        }
        if (i == word.size()) {
            // Backslashes at the very end sit against the closing quote, so they
            // are the escaping kind and are doubled.
            line.append(slashes * 2, '\\');
            break;
        }
        if (word[i] == '"') {
            line.append((slashes * 2) + 1, '\\');
        } else {
            line.append(slashes, '\\');
        }
        line += word[i];
    }
    line += '"';
}

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                             static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring wide(static_cast<size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                          wide.data(), length);
    return wide;
}

#endif  // _WIN32

}  // namespace

#ifdef _WIN32

CommandResult runCommand(const std::vector<std::string>& command) {
    if (command.empty()) return {};

    std::string line;
    for (const std::string& word : command) {
        if (!line.empty()) line += ' ';
        appendQuoted(line, word);
    }
    // CreateProcessW may write into the command line it is given, so it gets a
    // buffer of its own rather than a pointer into a temporary.
    std::wstring mutableLine = widen(line);
    mutableLine.push_back(L'\0');

    const InterruptsAside interrupts;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    // No application name, so that the first word is looked for on %PATH% with
    // %PATHEXT% applied — which is how the user would have typed it.
    if (::CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, TRUE, 0, nullptr,
                         nullptr, &startup, &process) == 0) {
        return failedToStart(lastErrorText());
    }

    // Unlike waitpid this cannot be cut short by a signal, so there is no retry.
    const DWORD waited = ::WaitForSingleObject(process.hProcess, INFINITE);
    const std::string reason = waited == WAIT_FAILED ? lastErrorText() : std::string();
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);

    if (!reason.empty()) return {CommandStage::NotWaited, reason};
    return {};
}

namespace {

/// One handle, closed however the function holding it leaves.
class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE handle) : handle_(handle) {}
    ~Handle() { close(); }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&&) = delete;
    Handle& operator=(Handle&&) = delete;

    [[nodiscard]] HANDLE get() const { return handle_; }
    HANDLE* out() {
        close();
        return &handle_;
    }
    void close() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) ::CloseHandle(handle_);
        handle_ = nullptr;
    }

private:
    HANDLE handle_{nullptr};
};

/// A pipe whose one end the child inherits and whose other end it does not:
/// the parent's own end held open in the child would keep it from ever seeing
/// the end of its input.
bool openPipe(Handle& read, Handle& write, bool childReads) {
    SECURITY_ATTRIBUTES inherit{};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;
    if (::CreatePipe(read.out(), write.out(), &inherit, 0) == 0) return false;
    HANDLE parentEnd = childReads ? write.get() : read.get();
    return ::SetHandleInformation(parentEnd, HANDLE_FLAG_INHERIT, 0) != 0;
}

/// The run behind `feedCommand()` and `readFromCommand()`, as on POSIX.
CommandOutput runPiped(const std::vector<std::string>& command, const std::string* input,
                       bool gather) {
    if (command.empty()) return {};

    std::string line;
    for (const std::string& word : command) {
        if (!line.empty()) line += ' ';
        appendQuoted(line, word);
    }
    std::wstring mutableLine = widen(line);
    mutableLine.push_back(L'\0');

    const auto notStarted = [] {
        return CommandOutput{CommandStage::NotStarted, lastErrorText(), {}};
    };

    SECURITY_ATTRIBUTES inherit{};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;
    Handle nowhere(::CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                                 OPEN_EXISTING, 0, nullptr));
    if (nowhere.get() == INVALID_HANDLE_VALUE) return notStarted();

    Handle inRead;
    Handle inWrite;
    if (input != nullptr && !openPipe(inRead, inWrite, /*childReads=*/true)) {
        return notStarted();
    }
    Handle outRead;
    Handle outWrite;
    if (gather && !openPipe(outRead, outWrite, /*childReads=*/false)) return notStarted();

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input != nullptr ? inRead.get() : nowhere.get();
    startup.hStdOutput = gather ? outWrite.get() : nowhere.get();
    startup.hStdError = nowhere.get();
    PROCESS_INFORMATION process{};

    // No console of its own: the program is not being given the terminal, and
    // one that opened a window of its own would flash one up over it.
    if (::CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, TRUE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) == 0) {
        return notStarted();
    }
    Handle processHandle(process.hProcess);
    Handle threadHandle(process.hThread);
    inRead.close();
    outWrite.close();
    nowhere.close();

    if (input != nullptr) {
        size_t written = 0;
        while (written < input->size()) {
            DWORD put = 0;
            const auto chunk =
                static_cast<DWORD>(std::min<size_t>(input->size() - written, 1U << 20U));
            if (::WriteFile(inWrite.get(), input->data() + written, chunk, &put,
                            nullptr) == 0) {
                break;
            }
            written += put;
        }
        inWrite.close();
    }

    CommandOutput result;
    if (gather) {
        char buffer[4096];
        DWORD got = 0;
        while (::ReadFile(outRead.get(), buffer, sizeof(buffer), &got, nullptr) != 0 &&
               got > 0) {
            result.output.append(buffer, got);
        }
        outRead.close();
    }

    if (::WaitForSingleObject(processHandle.get(), INFINITE) == WAIT_FAILED) {
        return CommandOutput{CommandStage::NotWaited, lastErrorText(), {}};
    }
    return result;
}

}  // namespace

CommandResult feedCommand(const std::vector<std::string>& command,
                          const std::string& input) {
    CommandOutput ran = runPiped(command, &input, /*gather=*/false);
    return {ran.stage, std::move(ran.reason)};
}

CommandOutput readFromCommand(const std::vector<std::string>& command) {
    return runPiped(command, nullptr, /*gather=*/true);
}

std::string userShellPath() {
    if (const char* named = std::getenv("SHELL"); named != nullptr && *named != '\0') {
        return named;
    }
    if (const char* comspec = std::getenv("COMSPEC"); comspec != nullptr && *comspec != '\0') {
        return comspec;
    }
    return "cmd.exe";
}

bool canExecute(const std::string& path) {
    // There is no execute bit to ask about; that a file is there and readable is
    // as far as this goes, and the rest shows up as a process that would not
    // start — which is reported either way.
    return ::_access(path.c_str(), 4) == 0;
}

#else

namespace {

/// The two ends of the pipe the child reports an exec failure down, closed
/// however the function leaves.
class Pipe {
public:
    Pipe() = default;
    ~Pipe() {
        closeRead();
        closeWrite();
    }

    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    Pipe(Pipe&&) = delete;
    Pipe& operator=(Pipe&&) = delete;

    /// Opens it, with the write end closed by a successful exec: that is what
    /// tells the two apart on the reading end — bytes mean the exec failed and
    /// end-of-file means the program started.
    [[nodiscard]] bool open() {
        if (::pipe(fd_) != 0) return false;
        return ::fcntl(fd_[1], F_SETFD, FD_CLOEXEC) == 0;
    }

    [[nodiscard]] int read() const { return fd_[0]; }
    [[nodiscard]] int write() const { return fd_[1]; }

    void closeRead() {
        if (fd_[0] >= 0) ::close(fd_[0]);
        fd_[0] = -1;
    }
    void closeWrite() {
        if (fd_[1] >= 0) ::close(fd_[1]);
        fd_[1] = -1;
    }

private:
    int fd_[2]{-1, -1};
};

/// The errno the child wrote before giving up, or 0 where it wrote nothing —
/// which is the exec having worked and closed the pipe.
int execErrno(int fd) {
    char bytes[sizeof(int)]{};
    size_t got = 0;
    while (got < sizeof(bytes)) {
        const ssize_t taken = ::read(fd, bytes + got, sizeof(bytes) - got);
        if (taken == 0) break;  // end of file: the program is running
        if (taken < 0) {
            if (errno == EINTR) continue;
            break;
        }
        got += static_cast<size_t>(taken);
    }
    if (got < sizeof(bytes)) return 0;
    int reported = 0;
    std::memcpy(&reported, bytes, sizeof(reported));
    return reported;
}

}  // namespace

CommandResult runCommand(const std::vector<std::string>& command) {
    if (command.empty()) return {};

    // Built here rather than in the child: everything after the fork runs
    // between two processes sharing a heap, and there is nothing to allocate
    // once the pointers are already standing.
    std::vector<std::string> words = command;
    std::vector<char*> argv;
    argv.reserve(words.size() + 1);
    for (std::string& word : words) argv.push_back(word.data());
    argv.push_back(nullptr);

    // What went wrong is asked of the child rather than read off what it
    // exited with: a program that never started and a program that ran and said
    // 127 are the same number on the way back, and the name is looked for on
    // $PATH — so there is no file here to have asked about beforehand.
    Pipe pipe;
    if (!pipe.open()) return failedToStart(std::strerror(errno));

    const InterruptsAside interrupts;

    const pid_t child = ::fork();
    if (child < 0) return failedToStart(std::strerror(errno));

    if (child == 0) {
        // The program answers for these itself; see InterruptsAside.
        interrupts.restore();
        pipe.closeRead();
        // $PATH is searched: `urlhandler lynx $url` names the program the way
        // the user would type it at a prompt, and a path with a slash in it is
        // taken as it stands all the same.
        ::execvp(argv[0], argv.data());
        const int failed = errno;
        // The parent has the screen, so this is the whole of what can be said.
        // Nothing checks the write: there is nowhere left to report it failing
        // to, and a parent that saw nothing come down the pipe reads a program
        // that started and ended — which is the one outcome not reported anyway.
        const ssize_t said = ::write(pipe.write(), &failed, sizeof(failed));
        static_cast<void>(said);
        // _exit rather than exit, since everything the fork copied belongs to
        // the parent.
        ::_exit(127);
    }

    // The parent's own copy of the write end goes first, or the read below
    // would wait for an end-of-file only this process is holding back.
    pipe.closeWrite();
    const int failed = execErrno(pipe.read());

    // Waited for whether or not it started: a child that could not exec still
    // has to be reaped.
    while (::waitpid(child, nullptr, 0) < 0) {
        // A signal arriving while waiting is not the program ending. Anything
        // else is: there is one child, and it is this one.
        if (errno != EINTR) return {CommandStage::NotWaited, std::strerror(errno)};
    }

    if (failed != 0) return failedToStart(std::strerror(failed));
    return {};
}

namespace {

/// One descriptor, closed however the function holding it leaves.
class Descriptor {
public:
    Descriptor() = default;
    explicit Descriptor(int fd) : fd_(fd) {}
    ~Descriptor() { close(); }

    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    Descriptor(Descriptor&&) = delete;
    Descriptor& operator=(Descriptor&&) = delete;

    [[nodiscard]] int get() const { return fd_; }
    void reset(int fd) {
        close();
        fd_ = fd;
    }
    void close() {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }

private:
    int fd_{-1};
};

/// A pipe of two descriptors, the ends closed apart as each is done with.
struct DataPipe {
    Descriptor read;
    Descriptor write;

    [[nodiscard]] bool open() {
        int fd[2]{-1, -1};
        if (::pipe(fd) != 0) return false;
        read.reset(fd[0]);
        write.reset(fd[1]);
        return true;
    }
};

/// SIGPIPE put aside for as long as one is held: a program that exits without
/// reading what it was handed would otherwise take AmberEdit down with the
/// write, where an `EPIPE` is what the write should answer.
class BrokenPipeAside {
public:
    BrokenPipeAside() : previous_(std::signal(SIGPIPE, SIG_IGN)) {}
    ~BrokenPipeAside() {
        if (previous_ != SIG_ERR) std::signal(SIGPIPE, previous_);
    }

    BrokenPipeAside(const BrokenPipeAside&) = delete;
    BrokenPipeAside& operator=(const BrokenPipeAside&) = delete;
    BrokenPipeAside(BrokenPipeAside&&) = delete;
    BrokenPipeAside& operator=(BrokenPipeAside&&) = delete;

private:
    void (*previous_)(int);
};

/// The run behind `feedCommand()` and `readFromCommand()`: `input` written to
/// the program where there is one and its standard input empty where there is
/// not, its standard output gathered where `gather` asks and thrown away where
/// it does not, and its standard error thrown away always.
CommandOutput runPiped(const std::vector<std::string>& command, const std::string* input,
                       bool gather) {
    if (command.empty()) return {};

    std::vector<std::string> words = command;
    std::vector<char*> argv;
    argv.reserve(words.size() + 1);
    for (std::string& word : words) argv.push_back(word.data());
    argv.push_back(nullptr);

    const auto notStarted = [](const char* reason) {
        return CommandOutput{CommandStage::NotStarted, reason, {}};
    };

    Pipe report;
    if (!report.open()) return notStarted(std::strerror(errno));
    Descriptor nowhere(::open("/dev/null", O_RDWR));
    if (nowhere.get() < 0) return notStarted(std::strerror(errno));
    DataPipe in;
    if (input != nullptr && !in.open()) return notStarted(std::strerror(errno));
    DataPipe out;
    if (gather && !out.open()) return notStarted(std::strerror(errno));

    const pid_t child = ::fork();
    if (child < 0) return notStarted(std::strerror(errno));

    if (child == 0) {
        report.closeRead();
        ::dup2(input != nullptr ? in.read.get() : nowhere.get(), STDIN_FILENO);
        ::dup2(gather ? out.write.get() : nowhere.get(), STDOUT_FILENO);
        ::dup2(nowhere.get(), STDERR_FILENO);
        // Nothing but the three above and the report pipe goes across the exec:
        // the parent's own ends held open in the child would keep the program
        // from ever seeing the end of its input.
        in.read.close();
        in.write.close();
        out.read.close();
        out.write.close();
        nowhere.close();
        ::execvp(argv[0], argv.data());
        const int failed = errno;
        const ssize_t said = ::write(report.write(), &failed, sizeof(failed));
        static_cast<void>(said);
        ::_exit(127);
    }

    report.closeWrite();
    in.read.close();
    out.write.close();
    nowhere.close();
    const int failed = execErrno(report.read());

    if (input != nullptr) {
        // Written whether or not the program started: one that did not is a
        // write that answers EPIPE, and the pipe is closed either way so that
        // the program that did sees the end of what it was given.
        const BrokenPipeAside aside;
        size_t written = 0;
        while (failed == 0 && written < input->size()) {
            const ssize_t put =
                ::write(in.write.get(), input->data() + written, input->size() - written);
            if (put < 0) {
                if (errno == EINTR) continue;
                break;
            }
            written += static_cast<size_t>(put);
        }
        in.write.close();
    }

    CommandOutput result;
    if (gather) {
        char buffer[4096];
        while (true) {
            const ssize_t got = ::read(out.read.get(), buffer, sizeof(buffer));
            if (got == 0) break;
            if (got < 0) {
                if (errno == EINTR) continue;
                break;
            }
            result.output.append(buffer, static_cast<size_t>(got));
        }
        out.read.close();
    }

    while (::waitpid(child, nullptr, 0) < 0) {
        if (errno != EINTR) {
            return CommandOutput{CommandStage::NotWaited, std::strerror(errno), {}};
        }
    }
    if (failed != 0) return notStarted(std::strerror(failed));
    return result;
}

}  // namespace

CommandResult feedCommand(const std::vector<std::string>& command,
                          const std::string& input) {
    CommandOutput ran = runPiped(command, &input, /*gather=*/false);
    return {ran.stage, std::move(ran.reason)};
}

CommandOutput readFromCommand(const std::vector<std::string>& command) {
    return runPiped(command, nullptr, /*gather=*/true);
}

std::string userShellPath() {
    // The environment is what the user has arranged for themselves; the password
    // file is what was arranged for them.
    if (const char* named = std::getenv("SHELL"); named != nullptr && *named != '\0') {
        return named;
    }
    const passwd* entry = ::getpwuid(::getuid());
    if (entry != nullptr && entry->pw_shell != nullptr && *entry->pw_shell != '\0') {
        return entry->pw_shell;
    }
    return "/bin/sh";
}

bool canExecute(const std::string& path) {
    return ::access(path.c_str(), X_OK) == 0;
}

#endif  // _WIN32

}  // namespace amberedit::sys
