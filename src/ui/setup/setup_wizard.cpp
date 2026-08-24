#include "ui/setup/setup_wizard.hpp"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "config/text_util.hpp"
#include "nodelist/nodelist_source.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/input_field.hpp"
#include "ui/setup/wizard_checks.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::setup {

using namespace term;

namespace {

namespace fs = std::filesystem;

/// How wide the box stands inside its frame, and the narrowest it is squeezed
/// to. A width and not a measurement of what is in it: the box is the same size
/// on every step, so nothing walks about the screen as the questions go by.
constexpr int kInnerWidth = 60;
constexpr int kMinInner = 44;
constexpr int kFrame = 2;

/// What the wizard will not draw in. Below this the box would have no room for
/// the listing and the answer under it both, and a wizard that cannot show what
/// it is asking about is worse than a sentence saying so.
constexpr int kMinWidth = 46;
constexpr int kMinHeight = 14;

/// Rows of a step with a listing in it that are not the listing: the title, the
/// two lines above the box, the path, two rules, the buttons and the bottom of
/// the frame.
constexpr int kChromeRows = 9;
constexpr int kWindowMargin = 2;

/// What the bottom rule says the keys do, which is not the same on every step:
/// inside a listing Enter is what opens a directory and picks a file, and on the
/// last step it is what writes the config.
const char* hintFor(const SetupState& state) {
    switch (state.stop) {
        case Stop::Picker: return "Enter open · Tab move · Esc leave";
        case Stop::Back: return "Enter goes back · Tab move · Esc leave";
        case Stop::Skip: return "Enter skips this · Tab move · Esc leave";
        case Stop::Next:
            return state.step == Step::Summary
                       ? "Enter writes the config · Tab move · Esc leave"
                       : "Enter goes on · Tab move · Esc leave";
        default: break;
    }
    return "Enter next field · Tab move · Esc leave";
}
constexpr const char* kLeaving = "Esc again to leave — nothing has been written";

/// The three tosser formats, in the order the step offers them.
constexpr config::TosserConfigFormat kFormats[] = {
    config::TosserConfigFormat::Fidoconfig,
    config::TosserConfigFormat::AreasBbs,
    config::TosserConfigFormat::SquishCfg,
};

const char* formatLabel(config::TosserConfigFormat format) {
    switch (format) {
        case config::TosserConfigFormat::AreasBbs: return "areas.bbs";
        case config::TosserConfigFormat::SquishCfg: return "squish.cfg";
        case config::TosserConfigFormat::Fidoconfig: break;
    }
    return "HPT (Fidoconfig)";
}

/// What the file step says it is looking for.
std::string lookingFor(config::TosserConfigFormat format) {
    switch (format) {
        case config::TosserConfigFormat::AreasBbs: return "Looking for an areas.bbs.";
        case config::TosserConfigFormat::SquishCfg: return "Looking for a squish.cfg.";
        case config::TosserConfigFormat::Fidoconfig: break;
    }
    return "An HPT config is called whatever your sysop called it.";
}

/// Settles how big the box is, once — and again only where the window itself
/// has changed size, exactly as every other dialog measures itself.
void fitBox(SetupState& state) {
    if (state.layoutWidth == state.width && state.layoutHeight == state.height) return;
    state.layoutWidth = state.width;
    state.layoutHeight = state.height;
    state.inner = std::min(kInnerWidth, std::max(kMinInner, state.width - kFrame));
    state.rows = std::max(1, state.height - kChromeRows - kWindowMargin);
}

/// The charset field the step being asked is about.
TextField& charsetField(SetupState& state) {
    return state.step == Step::ReadCharset ? state.readCharset : state.composeCharset;
}

/// The ring of the step that is up, in the order the rows are drawn.
std::vector<Stop> stopsFor(const SetupState& state) {
    switch (state.step) {
        case Step::Identity:
            // No Back: there is nothing behind the first question, and a stop
            // that does nothing is a stop that has to be explained.
            return {Stop::Name, Stop::Address, Stop::Format, Stop::Next};
        case Step::TosserFile: return {Stop::Picker, Stop::Back, Stop::Next};
        case Step::ReadCharset:
        case Step::ComposeCharset: return {Stop::Charset, Stop::Back, Stop::Next};
        case Step::Nodelist:
            return {Stop::Picker, Stop::NodelistDb, Stop::Skip, Stop::Back, Stop::Next};
        case Step::Summary: break;
    }
    return {Stop::Target, Stop::Back, Stop::Next};
}

/// Whether the stop is something to answer rather than something to press.
bool asksForSomething(Stop stop) {
    switch (stop) {
        case Stop::Name:
        case Stop::Address:
        case Stop::Format:
        case Stop::Picker:
        case Stop::Charset:
        case Stop::NodelistDb:
        case Stop::Target: return true;
        case Stop::Skip:
        case Stop::Back:
        case Stop::Next: break;
    }
    return false;
}

/// Where Enter goes from the stop the typing is on: the next thing the step
/// asks for, and Next once the step has asked everything.
///
/// Enter walks the questions rather than answering them, so that the middle of
/// a half-filled step is never told what is wrong with it — a name typed and an
/// address not yet is not a mistake, it is a step being answered. The checks are
/// Next's, which is where the user says the step is done. Back and Skip are
/// stepped over: neither is anywhere Enter should land somebody who is typing.
Stop enterGoesTo(const SetupState& state) {
    const std::vector<Stop> ring = stopsFor(state);
    const auto at = std::find(ring.begin(), ring.end(), state.stop);
    if (at != ring.end()) {
        for (auto next = at + 1; next != ring.end(); ++next) {
            if (asksForSomething(*next)) return *next;
        }
    }
    return Stop::Next;
}

void focusAfter(SetupState& state, int step) {
    const std::vector<Stop> ring = stopsFor(state);
    const auto at = std::find(ring.begin(), ring.end(), state.stop);
    const auto stops = static_cast<int>(ring.size());
    const int index = at == ring.end() ? 0 : static_cast<int>(at - ring.begin());
    state.stop = ring[static_cast<size_t>(((index + step) % stops + stops) % stops)];
}

/// The stop a step starts on: the first thing there is to answer.
void enterStep(SetupState& state, Step step) {
    state.step = step;
    state.stop = stopsFor(state).front();
    state.error.clear();
}

/// The directory a picker opens in: where the last answer of the same kind
/// came from, so that going back to a step does not start the walk again.
std::string directoryOf(const std::string& path) {
    if (path.empty()) {
        std::error_code ec;
        return fs::current_path(ec).string();
    }
    return fs::path(path).parent_path().string();
}

void openTosserPicker(SetupState& state) {
    const config::TosserConfigFormat format = state.format;
    state.picker.accepts = [format](std::string_view name) {
        return acceptsTosserFile(format, name);
    };
    open(state.picker, directoryOf(state.tosserConfigPath));
}

void openNodelistPicker(SetupState& state) {
    // Everything: a nodelist is called what the zone that publishes it calls it,
    // and it may as easily be an archive with one inside.
    state.picker.accepts = nullptr;
    open(state.picker, directoryOf(state.nodelistPath));
}

/// What a file the listing offers answers on the step that is up.
///
/// One place for it because there are three ways to say the same thing — Enter
/// on the row, a second click on it, and leaving the step with it lit — and all
/// three mean the file.
void takeFile(SetupState& state, const std::string& path) {
    if (path.empty()) return;
    if (state.step == Step::TosserFile) {
        state.tosserConfigPath = path;
        return;
    }

    // The nodelist is written as the pattern it is one of, so that the config
    // still names today's nodelist a week from now.
    state.nodelistPath = nodelist::generalizedSpec(path);
    if (!state.nodelistDb.touched) {
        setFieldValue(state.nodelistDb,
                      abbreviateHome(defaultNodelistDb(state.nodelistPath)));
    }
}

/// The row the listing has lit, taken as the answer — what makes leaving a step
/// with a file under the cursor mean that file, whether it was clicked once or
/// arrowed to. A cursor on a directory answers nothing and leaves whatever was
/// picked before it standing.
void takeFileUnderCursor(SetupState& state) {
    takeFile(state, fileUnderCursor(state.picker));
}

/// The tosser config as it stands, said out loud where it will not do.
[[nodiscard]] Result<void> checkPickedTosser(SetupState& state) {
    if (state.tosserConfigPath.empty()) {
        return failure("pick your tosser's config — it is where the areas come from");
    }
    auto areas = checkTosserConfig(state.tosserConfigPath, state.format);
    if (!areas) return tl::make_unexpected(std::move(areas).error());
    state.tosserAreas = *areas;
    return {};
}

/// Writes the config, which is the last thing the wizard does.
[[nodiscard]] Result<std::string> save(SetupState& state) {
    const std::string target(config::text::trim(state.target.value));
    if (auto ok = checkTargetPath(target); !ok)
        return tl::make_unexpected(std::move(ok).error());

    // The template first, so that a config never names one that was not written.
    auto templatePath = ensureTemplate(target, state.programPath);
    if (!templatePath) return tl::make_unexpected(std::move(templatePath).error());

    config::ConfigAnswers answers = answersOf(state);
    answers.templatePath = abbreviateHome(*templatePath);
    if (auto written = config::writeConfig(target, answers); !written) {
        return tl::make_unexpected(std::move(written).error());
    }
    return target;
}

/// What Next does on the step that is up: the checks it has to pass, and where
/// it goes when they do.
Outcome advance(SetupState& state) {
    switch (state.step) {
        case Step::Identity: {
            if (const auto ok = checkName(state.name.value); !ok) {
                state.error = ok.error()->message();
                state.stop = Stop::Name;
                return Outcome::Ignored;
            }
            const auto address = checkAddress(state.address.value);
            if (!address) {
                state.error = address.error()->message();
                state.stop = Stop::Address;
                return Outcome::Ignored;
            }
            // The address is what the charset is guessed from, so the guess is
            // made again here — while nobody has typed over it.
            if (!state.readCharset.touched) {
                setFieldValue(state.readCharset, defaultReadCharset(*address));
            }
            openTosserPicker(state);
            enterStep(state, Step::TosserFile);
            return Outcome::Ignored;
        }
        case Step::TosserFile: {
            takeFileUnderCursor(state);
            if (const auto ok = checkPickedTosser(state); !ok) {
                state.error = ok.error()->message();
                return Outcome::Ignored;
            }
            enterStep(state, Step::ReadCharset);
            return Outcome::Ignored;
        }
        case Step::ReadCharset: {
            if (const auto ok = checkCharsetAnswer(state.readCharset.value); !ok) {
                state.error = ok.error()->message();
                return Outcome::Ignored;
            }
            if (!state.composeCharset.touched) {
                setFieldValue(state.composeCharset,
                              std::string(config::text::trim(state.readCharset.value)));
            }
            enterStep(state, Step::ComposeCharset);
            return Outcome::Ignored;
        }
        case Step::ComposeCharset: {
            if (const auto ok = checkCharsetAnswer(state.composeCharset.value); !ok) {
                state.error = ok.error()->message();
                return Outcome::Ignored;
            }
            openNodelistPicker(state);
            enterStep(state, Step::Nodelist);
            return Outcome::Ignored;
        }
        case Step::Nodelist: {
            takeFileUnderCursor(state);
            if (state.nodelistPath.empty()) {
                state.error =
                    "pick a nodelist, or Skip — AmberEdit reads mail without one";
                return Outcome::Ignored;
            }
            if (config::text::trim(state.nodelistDb.value).empty()) {
                state.error = "say where the compiled nodelist goes";
                state.stop = Stop::NodelistDb;
                return Outcome::Ignored;
            }
            enterStep(state, Step::Summary);
            return Outcome::Ignored;
        }
        case Step::Summary: break;
    }

    const auto written = save(state);
    if (!written) {
        state.error = written.error()->message();
        return Outcome::Ignored;
    }
    state.savedPath = *written;
    return Outcome::Saved;
}

/// Back, which asks nothing and checks nothing: what has been answered stays
/// answered, and the step it goes to is the one before it.
void retreat(SetupState& state) {
    switch (state.step) {
        case Step::Identity: return;
        case Step::TosserFile: enterStep(state, Step::Identity); return;
        case Step::ReadCharset:
            openTosserPicker(state);
            enterStep(state, Step::TosserFile);
            return;
        case Step::ComposeCharset: enterStep(state, Step::ReadCharset); return;
        case Step::Nodelist: enterStep(state, Step::ComposeCharset); return;
        case Step::Summary: break;
    }
    openNodelistPicker(state);
    enterStep(state, Step::Nodelist);
}

/// Skip on the nodelist step: no nodelist, and nothing about one in the config.
void skipNodelist(SetupState& state) {
    state.nodelistPath.clear();
    setFieldValue(state.nodelistDb, "");
    enterStep(state, Step::Summary);
}

/// What picking a file means, which is a different thing on each of the two
/// steps that has a picker.
Outcome picked(SetupState& state) {
    takeFile(state, state.picker.chosen);
    // Onto the next thing to answer rather than on to the next step: what a
    // picked file is worth is Next's to say, here as everywhere else in the
    // wizard, and the listing stays on the screen to pick again from. On the
    // nodelist step that is the row underneath — where the compiled file goes is
    // the other half of the same answer.
    state.stop = state.step == Step::TosserFile ? Stop::Next : Stop::NodelistDb;
    return Outcome::Ignored;
}

// --- drawing -----------------------------------------------------------------

/// A row of the box with a label in front of the field.
Element labelledField(const std::string& label, TextField& field, int inner,
                      bool active) {
    const int width = std::max(1, inner - displayWidth(label));
    return dialog::framed(hbox({text(label) | color(theme::palette.dialogLabel),
                                renderField(field, width, active)}));
}

/// A line of prose in the box — what the step is about, or an example of what
/// it wants.
Element note(const std::string& content, int inner, theme::Color tint) {
    return dialog::line(" " + content, inner, tint);
}

/// One of the radio marks. The brackets are ASCII on purpose: a terminal in a
/// single-byte charset has no bullet to draw, and this is where the answer is
/// read off.
Element radio(const std::string& label, bool chosen, bool focused, int inner, Box& box) {
    box = Box::Nowhere();
    auto row =
        text(padRight("   " + std::string(chosen ? "(*) " : "( ) ") + label, inner));
    if (focused) {
        return dialog::framed(std::move(row) | bold |
                              color(theme::palette.selectionText) |
                              bgcolor(theme::palette.selection) | reflect(box));
    }
    return dialog::framed(
        std::move(row) |
        color(chosen ? theme::palette.dialogLabel : theme::palette.dialogText) |
        reflect(box));
}

Element button(const std::string& label, bool focused, Box& box) {
    box = Box::Nowhere();
    auto element = text(" [ " + label + " ] ");
    if (focused) {
        return std::move(element) | bold | color(theme::palette.selectionText) |
               bgcolor(theme::palette.selection) | reflect(box);
    }
    return std::move(element) | color(theme::palette.dialogLabel) | reflect(box);
}

/// The row of buttons every step ends with, put against the right-hand side.
Element buttons(SetupState& state, int inner) {
    state.backBox = Box::Nowhere();
    state.nextBox = Box::Nowhere();
    state.skipBox = Box::Nowhere();

    Elements row;
    int used = 0;
    const auto add = [&](const std::string& label, Stop stop, Box& box) {
        row.push_back(button(label, state.stop == stop, box));
        used += displayWidth(" [ " + label + " ] ");
    };

    if (state.step == Step::Nodelist) add("Skip", Stop::Skip, state.skipBox);
    if (state.step != Step::Identity) add("Back", Stop::Back, state.backBox);
    add(state.step == Step::Summary ? "Save" : "Next", Stop::Next, state.nextBox);

    Elements line{text(std::string(static_cast<size_t>(std::max(0, inner - used)), ' '))};
    for (auto& element : row) line.push_back(std::move(element));
    return dialog::framed(hbox(std::move(line)));
}

/// The title of the step: what it is asking about, and how much of the wizard is
/// left.
///
/// The step's own name and not the program's — the box is the whole screen here,
/// so a title saying "AmberEdit setup" five times over would spend the one line
/// it has on what the user knew before they started.
std::string titleOf(const SetupState& state) {
    switch (state.step) {
        case Step::Identity: return " General parameters — step 1 of 6 ";
        case Step::TosserFile: return " Tosser config file — step 2 of 6 ";
        case Step::ReadCharset: return " Incoming charset — step 3 of 6 ";
        case Step::ComposeCharset: return " Outgoing charset — step 4 of 6 ";
        case Step::Nodelist: return " Nodelist file — step 5 of 6 ";
        case Step::Summary: break;
    }
    return " The config to write — step 6 of 6 ";
}

void renderIdentity(SetupState& state, Elements& lines, int inner) {
    lines.push_back(
        note("Who the messages you write are from.", inner, theme::palette.dialogText));
    lines.push_back(
        labelledField(" Name:    ", state.name, inner, state.stop == Stop::Name));
    lines.push_back(
        labelledField(" Address: ", state.address, inner, state.stop == Stop::Address));
    lines.push_back(note("e.g. John Doe, 2:382/736", inner, theme::palette.dialogHint));
    lines.push_back(dialog::divider(inner));
    lines.push_back(note("Your areas come from your tosser's config, which is:", inner,
                         theme::palette.dialogText));
    for (size_t i = 0; i < std::size(kFormats); ++i) {
        lines.push_back(radio(formatLabel(kFormats[i]), state.format == kFormats[i],
                              state.stop == Stop::Format, inner, state.formatBoxes[i]));
    }
}

void renderPickerStep(SetupState& state, Elements& lines, int inner,
                      const std::string& first, const std::string& second) {
    lines.push_back(note(first, inner, theme::palette.dialogText));
    lines.push_back(note(second, inner, theme::palette.dialogHint));
    state.picker.rows = state.rows;
    lines.push_back(setup::render(state.picker, inner));
}

void renderCharset(SetupState& state, Elements& lines, int inner) {
    const bool incoming = state.step == Step::ReadCharset;
    lines.push_back(note(incoming ? "The charset in which the message you READ, when it has"
                                  : "The charset in which the message you WRITE is",
                         inner, theme::palette.dialogText));
    if (incoming) {
        lines.push_back(note("no CHRS kludge — or it says something like IBMPC 2.", inner,
                             theme::palette.dialogText));
    }
    lines.push_back(labelledField(" Charset: ", charsetField(state), inner,
                                  state.stop == Stop::Charset));
    lines.push_back(note(incoming ? "e.g. CP866 or CP437 or LATIN-1"
                                  : "e.g. UTF-8 or CP866 or CP437 or LATIN-1",
                         inner, theme::palette.dialogHint));
}

void renderNodelist(SetupState& state, Elements& lines, int inner) {
    lines.push_back(note("A nodelist, to look addresses and sysops up in.", inner,
                         theme::palette.dialogText));
    lines.push_back(note("It may be a ZIP archive. Skip if you have none.", inner,
                         theme::palette.dialogHint));
    // A row shorter than the other picker step, the compiled file taking one.
    state.picker.rows = std::max(1, state.rows - 1);
    lines.push_back(setup::render(state.picker, inner));
    lines.push_back(dialog::divider(inner));
    lines.push_back(labelledField(" Compiled to: ", state.nodelistDb, inner,
                                  state.stop == Stop::NodelistDb));
}

/// A path in the room there is for it, cut at the front rather than the back:
/// what says which file a path names stands at the end of it, and the summary is
/// read to check exactly that.
std::string fitPath(const std::string& path, int room) {
    if (displayWidth(path) <= room) return path;
    size_t at = path.size();
    while (at > 0) {
        const size_t previous = prevChar(path, at);
        if (displayWidth(path.substr(previous)) > room - 1) break;
        at = previous;
    }
    return "…" + path.substr(at);
}

void renderSummary(SetupState& state, Elements& lines, int inner) {
    const auto say = [&](const std::string& label, const std::string& value) {
        const std::string written = padRight(label, 10);
        lines.push_back(note(written + fitPath(value, inner - displayWidth(written) - 1),
                             inner, theme::palette.dialogText));
    };
    say("Name:", state.name.value);
    say("Address:", state.address.value);
    say("Areas:",
        state.tosserConfigPath + " (" + std::to_string(state.tosserAreas) + ")");
    say("Charset:",
        state.readCharset.value + " read, " + state.composeCharset.value + " written");
    say("Nodelist:", state.nodelistPath.empty() ? "none" : state.nodelistPath);
    lines.push_back(dialog::divider(inner));
    lines.push_back(
        labelledField(" Config: ", state.target, inner, state.stop == Stop::Target));
    lines.push_back(note("The whole commented config, with your answers in it.", inner,
                         theme::palette.dialogHint));
}

/// What the wizard says instead of itself in a window it does not fit in.
Element tooSmall(const SetupState& state) {
    const std::string said = "The setup wizard needs " + std::to_string(kMinWidth) + "x" +
                             std::to_string(kMinHeight) + ", this window is " +
                             std::to_string(state.width) + "x" +
                             std::to_string(state.height) + ".";
    return vbox({text(said) | color(theme::palette.error),
                 text("Make it bigger, or Esc to leave.") |
                     color(theme::palette.dialogHint)}) |
           center;
}

// --- clicks ------------------------------------------------------------------

Outcome handleClick(SetupState& state, const MouseEvent& click) {
    for (size_t i = 0; i < state.formatBoxes.size(); ++i) {
        if (!state.formatBoxes[i].Contain(click.x, click.y)) continue;
        state.format = kFormats[i];
        state.stop = Stop::Format;
        return Outcome::Ignored;
    }
    if (state.backBox.Contain(click.x, click.y)) {
        state.stop = Stop::Back;
        retreat(state);
        return Outcome::Ignored;
    }
    if (state.skipBox.Contain(click.x, click.y)) {
        state.stop = Stop::Skip;
        skipNodelist(state);
        return Outcome::Ignored;
    }
    if (state.nextBox.Contain(click.x, click.y)) {
        state.stop = Stop::Next;
        return advance(state);
    }

    struct Field {
        Stop stop;
        TextField* field;
    };
    const Field fields[] = {
        {Stop::Name, &state.name},
        {Stop::Address, &state.address},
        {Stop::Charset, &charsetField(state)},
        {Stop::NodelistDb, &state.nodelistDb},
        {Stop::Target, &state.target},
    };
    for (const auto& field : fields) {
        if (!field.field->box.Contain(click.x, click.y)) continue;
        state.stop = field.stop;
        clickField(*field.field, click.x);
        return Outcome::Ignored;
    }

    if (state.step == Step::TosserFile || state.step == Step::Nodelist) {
        const PickOutcome outcome = handleClick(state.picker, click);
        if (outcome == PickOutcome::Unclaimed) return Outcome::Ignored;
        state.stop = Stop::Picker;
        return outcome == PickOutcome::Picked ? picked(state) : Outcome::Ignored;
    }
    // Anywhere else: swallowed, as every event is while this is the screen.
    return Outcome::Ignored;
}

}  // namespace

void begin(SetupState& state, const std::string& programPath) {
    state.programPath = programPath;
    // Where the config goes unless the user says otherwise: `amberedit.cfg` in
    // the directory they are standing in, which is the first of the three places
    // AmberEdit looks and the one they can see.
    setFieldValue(state.target, "amberedit.cfg");
    enterStep(state, Step::Identity);
}

config::ConfigAnswers answersOf(const SetupState& state) {
    config::ConfigAnswers answers;
    answers.userName = std::string(config::text::trim(state.name.value));
    answers.address = std::string(config::text::trim(state.address.value));
    answers.tosserConfigPath = abbreviateHome(state.tosserConfigPath);
    answers.tosserFormat = state.format;
    answers.defaultCharset = std::string(config::text::trim(state.readCharset.value));
    answers.composeCharset = std::string(config::text::trim(state.composeCharset.value));
    if (!state.nodelistPath.empty()) {
        answers.nodelistPath = abbreviateHome(state.nodelistPath);
        answers.nodelistDbPath = std::string(config::text::trim(state.nodelistDb.value));
    }
    return answers;
}

Element render(SetupState& state) {
    fitBox(state);
    // Every control is put back where it cannot be clicked before anything is
    // drawn: a box holds where it was last drawn, and a step that does not draw
    // a field would otherwise leave the last step's field to be clicked on.
    for (auto& box : state.formatBoxes) box = Box::Nowhere();
    state.backBox = Box::Nowhere();
    state.nextBox = Box::Nowhere();
    state.skipBox = Box::Nowhere();
    state.name.box = Box::Nowhere();
    state.address.box = Box::Nowhere();
    state.readCharset.box = Box::Nowhere();
    state.composeCharset.box = Box::Nowhere();
    state.nodelistDb.box = Box::Nowhere();
    state.target.box = Box::Nowhere();
    state.picker.path.box = Box::Nowhere();
    state.picker.rowBoxes.clear();

    Element body;
    if (state.width < kMinWidth || state.height < kMinHeight) {
        body = tooSmall(state);
    } else {
        const int inner = state.inner;
        Elements lines{dialog::titleBar(titleOf(state), inner)};

        switch (state.step) {
            case Step::Identity: renderIdentity(state, lines, inner); break;
            case Step::TosserFile:
                renderPickerStep(state, lines, inner, "Where your tosser's config is.",
                                 lookingFor(state.format));
                break;
            case Step::ReadCharset:
            case Step::ComposeCharset: renderCharset(state, lines, inner); break;
            case Step::Nodelist: renderNodelist(state, lines, inner); break;
            case Step::Summary: renderSummary(state, lines, inner); break;
        }

        lines.push_back(dialog::divider(inner));
        lines.push_back(buttons(state, inner));
        // The picker has trouble of its own to report — a path that is not
        // there — and one bottom rule says both: it is the same rule the answer
        // that caused it is being corrected above.
        const std::string& said = state.error.empty() ? state.picker.error : state.error;
        lines.push_back(dialog::bottomBar(hintFor(state), said, inner));
        body = dialog::surface(vbox(std::move(lines))) | center;
    }

    return std::move(body) | bgcolor(theme::palette.background) |
           color(theme::palette.text);
}

Outcome handleEvent(SetupState& state, const Event& event) {
    if (const auto click = leftClick(event)) return handleClick(state, *click);

    // What went wrong was about the answer as it stood, and the answer is being
    // changed: a corrected field should stop being shouted at.
    state.error.clear();
    state.picker.error.clear();
    if (event != Event::Escape) state.leaving = false;

    if (event == Event::Tab) {
        focusAfter(state, 1);
        return Outcome::Ignored;
    }
    if (event == Event::TabReverse) {
        focusAfter(state, -1);
        return Outcome::Ignored;
    }
    if (event == Event::Escape) {
        // A search half typed into the listing is what Esc most plainly means
        // where there is one.
        if (state.stop == Stop::Picker && !state.picker.search.empty()) {
            state.picker.search.clear();
            return Outcome::Ignored;
        }
        // Answers on the screen and nothing on disk: leaving takes saying so
        // twice.
        if (!state.leaving) {
            state.leaving = true;
            state.error = kLeaving;
            return Outcome::Ignored;
        }
        return Outcome::Cancelled;
    }

    if (state.stop == Stop::Picker) {
        const PickOutcome outcome = handleEvent(state.picker, event);
        if (outcome == PickOutcome::Picked) return picked(state);
        if (outcome == PickOutcome::Ignored) return Outcome::Ignored;
        // Unclaimed: the wizard's own keys, below.
    }

    if (event == Event::Return) {
        if (state.stop == Stop::Back) {
            retreat(state);
            return Outcome::Ignored;
        }
        if (state.stop == Stop::Skip) {
            skipNodelist(state);
            return Outcome::Ignored;
        }
        if (state.stop == Stop::Next) return advance(state);
        // Anywhere else Enter is what moves on to the next thing the step asks
        // for. The step is checked when Next is pressed and not before.
        state.stop = enterGoesTo(state);
        return Outcome::Ignored;
    }

    switch (state.stop) {
        case Stop::Name:
            if (handleFieldKey(state.name, event)) return Outcome::Ignored;
            break;
        case Stop::Address:
            if (handleFieldKey(state.address, event)) return Outcome::Ignored;
            break;
        case Stop::Charset:
            if (handleFieldKey(charsetField(state), event)) return Outcome::Ignored;
            break;
        case Stop::NodelistDb:
            if (handleFieldKey(state.nodelistDb, event)) return Outcome::Ignored;
            break;
        case Stop::Target:
            if (handleFieldKey(state.target, event)) return Outcome::Ignored;
            break;
        case Stop::Format:
            // The radio, which is walked with the arrows and set with a space —
            // and set by walking, since a mark nothing stands on says nothing.
            if (event == Event::ArrowUp || event == Event::ArrowDown) {
                const auto* at =
                    std::find(std::begin(kFormats), std::end(kFormats), state.format);
                const auto stops = static_cast<int>(std::size(kFormats));
                const int index =
                    at == std::end(kFormats) ? 0 : static_cast<int>(at - kFormats);
                const int step = event == Event::ArrowUp ? -1 : 1;
                state.format = kFormats[static_cast<size_t>(
                    ((index + step) % stops + stops) % stops)];
                return Outcome::Ignored;
            }
            break;
        case Stop::Picker:
        case Stop::Skip:
        case Stop::Back:
        case Stop::Next: break;
    }

    // What is left moves between the stops, so that a step is walked with the
    // arrows as well as with Tab.
    if (event == Event::ArrowDown) {
        focusAfter(state, 1);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowUp) {
        focusAfter(state, -1);
        return Outcome::Ignored;
    }
    if (event == Event::Character(' ')) {
        if (state.stop == Stop::Back) {
            retreat(state);
            return Outcome::Ignored;
        }
        if (state.stop == Stop::Skip) {
            skipNodelist(state);
            return Outcome::Ignored;
        }
        if (state.stop == Stop::Next) return advance(state);
    }
    return Outcome::Ignored;
}

}  // namespace amberedit::ui::setup
