#include "app/user_shell.hpp"

#include <pwd.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>

namespace amberedit::app {
namespace {

/// The shell the password file gives this user, or nothing where it names none.
/// Read only when `$SHELL` is unset: the environment is what the user has
/// arranged for themselves, and the password file is what was arranged for them.
std::string shellFromPasswd() {
    const passwd* entry = getpwuid(getuid());
    if (entry == nullptr || entry->pw_shell == nullptr) return {};
    return entry->pw_shell;
}

/// Puts SIGINT and SIGQUIT aside for as long as the shell is running, and back
/// afterwards.
///
/// The child is left in AmberEdit's own process group, so a Ctrl-C typed at the
/// shell's prompt reaches this process as well — and the reader is not what the
/// user meant to interrupt. An interactive shell takes over the terminal's
/// foreground group as it starts and answers for the keys after that; this
/// covers the moment before it has, and the shells that never do.
///
/// It is the parent that ignores them, and the child that has to put them back
/// before exec: an ignored signal stays ignored across an exec, and a shell
/// started deaf to Ctrl-C would stay deaf for as long as it ran.
class InterruptsAside {
public:
    InterruptsAside() {
        interrupt_ = std::signal(SIGINT, SIG_IGN);
        quit_ = std::signal(SIGQUIT, SIG_IGN);
    }
    ~InterruptsAside() { restore(); }

    InterruptsAside(const InterruptsAside&) = delete;
    InterruptsAside& operator=(const InterruptsAside&) = delete;
    InterruptsAside(InterruptsAside&&) = delete;
    InterruptsAside& operator=(InterruptsAside&&) = delete;

    void restore() const {
        if (interrupt_ != SIG_ERR) std::signal(SIGINT, interrupt_);
        if (quit_ != SIG_ERR) std::signal(SIGQUIT, quit_);
    }

private:
    void (*interrupt_)(int){SIG_ERR};
    void (*quit_)(int){SIG_ERR};
};

}  // namespace

std::string userShellPath() {
    const char* named = std::getenv("SHELL");
    if (named != nullptr && *named != '\0') return named;
    const std::string passwd = shellFromPasswd();
    if (!passwd.empty()) return passwd;
    return "/bin/sh";
}

Result<void> runUserShell() {
    const std::string shell = userShellPath();

    // Asked before the fork rather than worked out from what the child exited
    // with: a shell that never started and a shell that ran and said 127 are
    // the same number on the way back, and only one of the two is worth
    // reporting. Here there is still an errno to say which of the two it is.
    if (access(shell.c_str(), X_OK) != 0) {
        return failure("cannot run " + shell + ": " + std::strerror(errno));
    }

    const InterruptsAside interrupts;

    const pid_t child = fork();
    if (child < 0) {
        return failure("cannot run " + shell + ": " + std::strerror(errno));
    }
    if (child == 0) {
        // The shell answers for these itself; see InterruptsAside.
        interrupts.restore();
        // argv[0] is the shell's own path rather than a "-" name: this is an
        // interactive shell and not a login one — the user is logged in already,
        // and running their profile again would be a second login they did not
        // ask for.
        char* const argv[] = {const_cast<char*>(shell.c_str()), nullptr};
        execv(shell.c_str(), argv);
        // Only reached where exec failed, and there is nothing left in this
        // process to say so with: the parent has the screen. _exit rather than
        // exit, since everything the fork copied belongs to the parent.
        _exit(127);
    }

    // Nothing is read back off it: what the shell exited with is not this
    // program's business — see the header.
    while (waitpid(child, nullptr, 0) < 0) {
        // A signal arriving while waiting is not the shell ending. Anything
        // else is: there is one child, and it is this one.
        if (errno != EINTR) {
            return failure("cannot wait for " + shell + ": " + std::strerror(errno));
        }
    }
    return {};
}

}  // namespace amberedit::app
