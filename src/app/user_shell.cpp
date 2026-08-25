#include "app/user_shell.hpp"

#include <pwd.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "app/interrupts_aside.hpp"
#include "i18n/i18n.hpp"

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

}  // namespace

std::string userShellPath() {
    const char* named = std::getenv("SHELL");
    if (named != nullptr && *named != '\0') return named;
    const std::string passwd = shellFromPasswd();
    if (!passwd.empty()) return passwd;
    return "/bin/sh";
}

tl::expected<void, ErrorPtr> runUserShell() {
    const std::string shell = userShellPath();

    // Asked before the fork rather than worked out from what the child exited
    // with: a shell that never started and a shell that ran and said 127 are
    // the same number on the way back, and only one of the two is worth
    // reporting. Here there is still an errno to say which of the two it is.
    if (access(shell.c_str(), X_OK) != 0) {
        return failure(
            i18n::format(_("cannot run {0}: {1}"), {shell, std::strerror(errno)}));
    }

    const InterruptsAside interrupts;

    const pid_t child = fork();
    if (child < 0) {
        return failure(
            i18n::format(_("cannot run {0}: {1}"), {shell, std::strerror(errno)}));
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
            return failure(i18n::format(_("cannot wait for {0}: {1}"),
                                        {shell, std::strerror(errno)}));
        }
    }
    return {};
}

}  // namespace amberedit::app
