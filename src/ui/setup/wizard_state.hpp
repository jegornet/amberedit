#pragma once

#include <array>
#include <string>

#include "config/app_config.hpp"
#include "ui/setup/file_picker.hpp"
#include "ui/term/box.hpp"
#include "ui/text_field.hpp"

namespace amberedit::ui::setup {

/// Which of the wizard's questions is being asked.
///
/// Six numbered steps, the last of which is the config the other five answered:
/// the numbers are a promise about how much is left, and writing the file is a
/// step like the rest — the one the user is walking towards.
enum class Step {
    Identity,
    TosserFile,
    ReadCharset,
    ComposeCharset,
    Nodelist,
    Summary,
};

/// Everything the typing may be on, across every step. Flat rather than one enum
/// per step because the ring is walked the same way wherever it is, and each
/// step says which of these it has.
enum class Stop {
    Name,
    Address,
    Format,
    Picker,
    Charset,
    NodelistDb,
    Skip,
    Target,
    Back,
    Next,
};

/// The wizard, as far as it has been answered.
///
/// It stands where `AppState` stands for every other dialog — passed by
/// reference to the drawing and to the dispatch — and is deliberately not that
/// one: there is no config yet, so no area manager, no key map and no theme, and
/// the three of them are what `AppState` is made of.
struct SetupState {
    Step step{Step::Identity};
    Stop stop{Stop::Name};

    TextField name;
    TextField address;
    TextField readCharset;
    TextField composeCharset;
    TextField nodelistDb;
    TextField target;

    config::TosserConfigFormat format{config::TosserConfigFormat::Fidoconfig};
    /// The tosser config that was picked, and how many areas were read out of it
    /// when it was — which the summary says, since it is the one number that
    /// tells the user they picked the right file.
    std::string tosserConfigPath;
    size_t tosserAreas{0};
    /// The nodelist, already generalized into the pattern that will still be
    /// today's nodelist tomorrow. Empty where the step was skipped.
    std::string nodelistPath;

    FilePicker picker;

    /// argv[0], for finding the installed message template.
    std::string programPath;
    /// The config that was written, once one has been.
    std::string savedPath;

    /// What the last thing tried is answered with, in the bottom rule.
    std::string error;
    /// Whether Esc has been pressed once. Nothing has been written at that
    /// point and every answer is on the screen, so leaving takes two presses.
    bool leaving{false};

    /// The window, and the box measured against it.
    int width{80};
    int height{24};
    int inner{0};
    int rows{0};
    int layoutWidth{0};
    int layoutHeight{0};

    /// Where the controls were last drawn, for the click that comes after.
    std::array<term::Box, 3> formatBoxes{term::Box::Nowhere(), term::Box::Nowhere(),
                                         term::Box::Nowhere()};
    term::Box backBox{term::Box::Nowhere()};
    term::Box nextBox{term::Box::Nowhere()};
    term::Box skipBox{term::Box::Nowhere()};
};

}  // namespace amberedit::ui::setup
