#include "ui/export_mode_dialog.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app/export_file.hpp"
#include "ui/event_util.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::export_mode_dialog {

using namespace term;

namespace {

using Mode = AppState::ExportPicker::Mode;

/// How many names the box lists before it starts counting the rest. Eight is
/// what a message with a handful of files in it takes; past that the list is no
/// longer telling the user anything they are reading name by name, and a box
/// that grew a row per file would walk off a short window.
constexpr int kMaxNames = 8;
/// The widest a name is drawn. A name longer than this is truncated like every
/// other label in the interface — it is being shown rather than typed into, and
/// the file is written under the whole of it either way.
constexpr int kNameWidth = 48;

/// The two answers in the order they are drawn and stepped through. Files
/// stands first and is what the box opens on: a message carrying a file is a
/// message somebody sent a file in, and taking it out is what the question was
/// raised for.
constexpr Mode kModes[] = {Mode::Uue, Mode::Text};
constexpr int kModeCount = 2;

int indexOf(Mode mode) {
    return mode == Mode::Uue ? 0 : 1;
}

/// One of the two answers, drawn as the forward dialog's three are — the same
/// fill under whatever Enter would act on, and the same color under a click
/// being shown before it acts.
Element button(const std::string& label, bool selected, bool pressed) {
    auto element = text("  " + label + "  ");
    // Innermost, so that it is the color that lands: a parent paints its whole
    // box and the child paints over it.
    if (pressed) element = std::move(element) | color(theme::palette.animatedButtonText);
    if (selected) {
        return std::move(element) | bold | color(theme::palette.selectionText) |
               bgcolor(theme::palette.selection);
    }
    return std::move(element) | color(theme::palette.text);
}

void step(AppState::ExportModePicker& picker, int delta) {
    const int next = (indexOf(picker.mode) + delta + kModeCount) % kModeCount;
    picker.mode = kModes[next];
}

/// The answer a letter names, or nothing. The initial of each, which is what the
/// hint line under the buttons prints.
std::optional<Mode> modeFor(const Event& event) {
    if (event == Event::Character('f') || event == Event::Character('F')) {
        return Mode::Uue;
    }
    if (event == Event::Character('t') || event == Event::Character('T')) {
        return Mode::Text;
    }
    return std::nullopt;
}

/// The line over the names. It says what the encoding is called rather than how
/// many files there are: what the question turns on is that the message holds
/// something that is not text at all, and the names under it are the count.
constexpr const char* kHeading = "The message contains UUE-encoded file(s):";

/// The names as they are listed, the last of them counting whatever is left
/// where there are more than the box shows.
std::vector<std::string> namesShown(const std::vector<app::UueFile>& files) {
    std::vector<std::string> rows;
    const auto total = static_cast<int>(files.size());
    if (total <= kMaxNames) {
        for (const auto& file : files)
            rows.push_back(truncateToWidth(file.name, kNameWidth));
        return rows;
    }

    for (int i = 0; i < kMaxNames - 1; ++i) {
        rows.push_back(truncateToWidth(files[static_cast<size_t>(i)].name, kNameWidth));
    }
    rows.push_back("… and " + std::to_string(total - (kMaxNames - 1)) + " more");
    return rows;
}

}  // namespace

void open(AppState& state, std::vector<app::UueFile> files) {
    if (!state.readHeader || !state.readBody || files.empty()) return;

    AppState::ExportModePicker picker;
    picker.files = std::move(files);
    state.exportModePicker = std::move(picker);
}

Element render(AppState& state, Element background) {
    AppState::ExportModePicker& picker = *state.exportModePicker;

    Elements rows{
        text(kHeading) | bold | color(theme::palette.text),
        text(""),
    };
    for (const auto& name : namesShown(picker.files)) {
        rows.push_back(text("  " + name) | color(theme::palette.header));
    }
    rows.push_back(text(""));

    const auto answer = [&](const std::string& label, Mode mode, term::Box& box) {
        // reflect() writes back where the button landed once the box has been
        // centred, which is what handleEvent() hit-tests a click on.
        return button(label, picker.mode == mode,
                      state.isPressed(AppState::Pressed::ExportChoice,
                                      static_cast<uint32_t>(indexOf(mode)))) |
               reflect(box);
    };
    rows.push_back(hbox({
                       answer("Files", Mode::Uue, picker.filesBox),
                       text("   "),
                       answer("Text", Mode::Text, picker.textBox),
                   }) |
                   center);
    rows.push_back(text(""));
    rows.push_back(text("←→ choose · Enter confirm · f/t · Esc cancel") |
                   color(theme::palette.footer));

    // The frame is drawn round a padded box: without the margins the hint line
    // sets the width and ends up flush against the border.
    auto dialog = hbox({text("  "), vbox(std::move(rows)), text("  ")}) | border |
                  color(theme::palette.separator);

    // clear_under wipes the screen behind the box, so the message underneath
    // does not show through it.
    return dbox({std::move(background), std::move(dialog) | clear_under | center});
}

Outcome handleEvent(AppState& state, const Event& event) {
    AppState::ExportModePicker& picker = *state.exportModePicker;

    // A click answers with the button it landed on, without selecting it first:
    // pointing at Text and pressing is one gesture, not two.
    if (const auto click = leftClick(event)) {
        const auto pick = [&](Mode mode) {
            // Selected first and shown pressed after, so that the button the
            // pointer landed on is the current one for the length of the
            // animation before the export dialog takes the screen.
            picker.mode = mode;
            state.showClick(AppState::Pressed::ExportChoice,
                            static_cast<uint32_t>(indexOf(mode)));
            return Outcome::Picked;
        };
        if (picker.filesBox.Contain(click->x, click->y)) return pick(Mode::Uue);
        if (picker.textBox.Contain(click->x, click->y)) return pick(Mode::Text);
        // Anywhere else, inside the box or outside it: swallowed, as every other
        // event is while the dialog is modal.
        return Outcome::Ignored;
    }

    // The initials answer outright, the way y and n answer a confirmation.
    if (const auto typed = modeFor(event)) {
        picker.mode = *typed;
        return Outcome::Picked;
    }
    if (event == Event::ArrowRight || event == Event::Tab) {
        step(picker, 1);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowLeft || event == Event::TabReverse) {
        step(picker, -1);
        return Outcome::Ignored;
    }
    if (event == Event::Return) return Outcome::Picked;
    if (event == Event::Escape || event == Event::Backspace) {
        state.exportModePicker.reset();
        return Outcome::Dismissed;
    }
    return Outcome::Ignored;
}

}  // namespace amberedit::ui::export_mode_dialog
