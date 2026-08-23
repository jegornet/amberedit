#include "ui/setup/setup_run.hpp"

#include <unistd.h>

#include <iostream>

#include "ui/setup/setup_wizard.hpp"
#include "ui/setup/wizard_state.hpp"
#include "ui/term/event.hpp"
#include "ui/term/terminal.hpp"

namespace amberedit::ui::setup {

int runSetup(const std::string& programPath) {
    // Asked before ncurses is: a wizard is a conversation, and there is nobody
    // at the other end of a pipe to have it with.
    if (::isatty(STDIN_FILENO) == 0 || ::isatty(STDOUT_FILENO) == 0) {
        std::cerr << "error: --setup asks questions, so it needs a terminal\n";
        return 2;
    }

    SetupState state;
    begin(state, programPath);

    {
        // The terminal is given back at the end of this block, which is what
        // lets everything below be said in plain text.
        term::Terminal terminal;
        while (terminal.running()) {
            state.width = terminal.width();
            state.height = terminal.height();
            terminal.draw(render(state));

            const Outcome outcome = handleEvent(state, terminal.poll());
            if (outcome == Outcome::Saved || outcome == Outcome::Cancelled) break;
        }
    }

    if (state.savedPath.empty()) {
        std::cerr << "nothing was written — AmberEdit still has no config\n";
        return 1;
    }
    std::cout << "written: " << state.savedPath << "\n"
              << "Start AmberEdit from this directory.\n";
    return 0;
}

}  // namespace amberedit::ui::setup
