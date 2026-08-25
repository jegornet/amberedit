#include "app/url_handler.hpp"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string_view>

#include "app/interrupts_aside.hpp"
#include "config/app_config.hpp"
#include "i18n/i18n.hpp"

namespace amberedit::app {
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
        return fcntl(fd_[1], F_SETFD, FD_CLOEXEC) == 0;
    }

    [[nodiscard]] int read() const { return fd_[0]; }
    [[nodiscard]] int write() const { return fd_[1]; }

    void closeRead() {
        if (fd_[0] >= 0) close(fd_[0]);
        fd_[0] = -1;
    }
    void closeWrite() {
        if (fd_[1] >= 0) close(fd_[1]);
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

std::vector<std::string> urlHandlerCommand(const std::vector<std::string>& handler,
                                           const std::string& url) {
    const std::string_view mark = config::AppConfig::kUrlPlaceholder;
    std::vector<std::string> command;
    command.reserve(handler.size());
    for (const std::string& word : handler) {
        std::string filled = word;
        for (size_t at = filled.find(mark); at != std::string::npos;
             at = filled.find(mark, at + url.size())) {
            filled.replace(at, mark.size(), url);
        }
        command.push_back(std::move(filled));
    }
    return command;
}

tl::expected<void, ErrorPtr> runUrlHandler(const std::vector<std::string>& handler,
                                           const std::string& url) {
    if (handler.empty()) return {};

    // Built here rather than in the child: everything after the fork runs
    // between two processes sharing a heap, and there is nothing to allocate
    // once the pointers are already standing.
    std::vector<std::string> command = urlHandlerCommand(handler, url);
    std::vector<char*> argv;
    argv.reserve(command.size() + 1);
    for (std::string& word : command) argv.push_back(word.data());
    argv.push_back(nullptr);
    const std::string& program = command.front();

    // What went wrong is asked of the child rather than read off what it
    // exited with: a program that never started and a program that ran and said
    // 127 are the same number on the way back, and the name is looked for on
    // $PATH — so there is no file here to have asked about beforehand.
    Pipe pipe;
    if (!pipe.open()) {
        return failure(
            i18n::format(_("cannot run {0}: {1}"), {program, std::strerror(errno)}));
    }

    const InterruptsAside interrupts;

    const pid_t child = fork();
    if (child < 0) {
        return failure(
            i18n::format(_("cannot run {0}: {1}"), {program, std::strerror(errno)}));
    }
    if (child == 0) {
        // The program answers for these itself; see InterruptsAside.
        interrupts.restore();
        pipe.closeRead();
        // $PATH is searched: `urlhandler lynx $url` names the program the way
        // the user would type it at a prompt, and a path with a slash in it is
        // taken as it stands all the same.
        execvp(argv[0], argv.data());
        const int failed = errno;
        // The parent has the screen, so this is the whole of what can be said.
        // Nothing checks the write: there is nowhere left to report it failing
        // to, and a parent that saw nothing come down the pipe reads a program
        // that started and ended — which is the one outcome not reported
        // anyway.
        const ssize_t said = ::write(pipe.write(), &failed, sizeof(failed));
        static_cast<void>(said);
        // _exit rather than exit, since everything the fork copied belongs to
        // the parent.
        _exit(127);
    }

    // The parent's own copy of the write end goes first, or the read below
    // would wait for an end-of-file only this process is holding back.
    pipe.closeWrite();
    const int failed = execErrno(pipe.read());

    // Waited for whether or not it started: a child that could not exec still
    // has to be reaped. Nothing is read back off it — what the program exited
    // with is not this program's business, see the header.
    while (waitpid(child, nullptr, 0) < 0) {
        // A signal arriving while waiting is not the program ending. Anything
        // else is: there is one child, and it is this one.
        if (errno != EINTR) {
            return failure(i18n::format(_("cannot wait for {0}: {1}"),
                                        {program, std::strerror(errno)}));
        }
    }
    if (failed != 0) {
        return failure(
            i18n::format(_("cannot run {0}: {1}"), {program, std::strerror(failed)}));
    }
    return {};
}

}  // namespace amberedit::app
