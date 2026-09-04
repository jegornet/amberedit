#include "app/user_shell.hpp"

#include <cerrno>
#include <cstring>

#include "i18n/i18n.hpp"
#include "sys/process.hpp"

namespace amberedit::app {

std::string userShellPath() {
    return sys::userShellPath();
}

tl::expected<void, ErrorPtr> runUserShell() {
    const std::string shell = userShellPath();

    // Asked before starting it rather than worked out from what it exited with:
    // a shell that never started and a shell that ran and said 127 are the same
    // number on the way back, and only one of the two is worth reporting. Here
    // there is still an errno to say which of the two it is.
    if (!sys::canExecute(shell)) {
        return failure(
            i18n::format(_("cannot run {0}: {1}"), {shell, std::strerror(errno)}));
    }

    // Run by its own path and with no arguments: this is an interactive shell
    // and not a login one — the user is logged in already, and running their
    // profile again would be a second login they did not ask for.
    const sys::CommandResult result = sys::runCommand({shell});

    switch (result.stage) {
        case sys::CommandStage::Started:
            return {};
        case sys::CommandStage::NotStarted:
            return failure(i18n::format(_("cannot run {0}: {1}"), {shell, result.reason}));
        case sys::CommandStage::NotWaited:
            return failure(
                i18n::format(_("cannot wait for {0}: {1}"), {shell, result.reason}));
    }
    return {};
}

}  // namespace amberedit::app
