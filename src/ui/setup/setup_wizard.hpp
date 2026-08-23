#pragma once

#include <string>

#include "config/config_writer.hpp"
#include "ui/setup/wizard_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// The wizard `--setup` runs: five questions, and a sixth step that writes the
/// config they answer.
///
/// It is drawn and dispatched the way every dialog in AmberEdit is — a render
/// that takes the state and gives back an element, and a handleEvent that
/// answers a key — so that the tests can walk it through without a terminal.
/// What owns a terminal is `runSetup`, and it is the only part of this that
/// cannot be tested.
namespace amberedit::ui::setup {

/// What an event did to the wizard.
enum class Outcome {
    /// Still asking.
    Ignored,
    /// The config has been written; `savedPath` says where.
    Saved,
    /// The user left without one.
    Cancelled,
};

/// The wizard at its first question.
void begin(SetupState& state, const std::string& programPath);

/// The whole screen: the box, centred, over the fill the palette names.
[[nodiscard]] term::Element render(SetupState& state);

/// One event: a click, the ring, or whatever the stop the typing is on takes.
Outcome handleEvent(SetupState& state, const term::Event& event);

/// The answers as they stand, which is what the config is written from. Public
/// for the summary and for the tests — nothing else needs to ask.
[[nodiscard]] config::ConfigAnswers answersOf(const SetupState& state);

}  // namespace amberedit::ui::setup
