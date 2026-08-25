#pragma once

#include <csignal>

/// SIGINT and SIGQUIT put aside while another program has the terminal.
///
/// Wanted by everything here that hands the screen over and waits — the user's
/// shell, the program a link is opened with — and written once rather than in
/// each of them: the reasoning below is the same reasoning either way.
namespace amberedit::app {

/// Puts SIGINT and SIGQUIT aside for as long as it lives, and back afterwards.
///
/// The child is left in AmberEdit's own process group, so a Ctrl-C typed while
/// it runs reaches this process as well — and the reader is not what the user
/// meant to interrupt. A program that takes over the terminal's foreground
/// group as it starts answers for the keys after that; this covers the moment
/// before it has, and the programs that never do.
///
/// It is the parent that ignores them, and the child that has to put them back
/// before exec: an ignored signal stays ignored across an exec, and a program
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

}  // namespace amberedit::app
