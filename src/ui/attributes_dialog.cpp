#include "ui/attributes_dialog.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "domain/message.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::attributes_dialog {

using namespace term;

namespace {

/// One attribute of the message: what it is called, the letter Ctrl is held
/// with to turn it over, and the FTS-0001 bit it stands for.
struct Attribute {
    const char* name;
    char key;
    uint32_t bit;
};

/// The attributes the dialog lists, in the order FTN editors have always listed
/// them — down the left column and then down the right, the chords being
/// GoldED's.
///
/// The attributes AmberEdit has no bit for are left out rather than shown dead:
/// Archive/Sent, Zonegate, Hub/Host-Route, Xmail, Erase and Truncate File/Sent,
/// Locked, Confirm Rcpt Request and the reserved ones are not in `domain::attr`
/// and there would be nothing for a keystroke on them to set.
///
/// Four of the chords are the C0 bytes that Backspace, Tab, Enter and line feed
/// have sent since ASCII — Hold, Immediate, Return Rcpt Request and Transit —
/// so they arrive as those keys unless the terminal reports modified keys
/// separately, and only reach here where it does. They are listed all the same:
/// the attribute is the same attribute wherever it is being written, the
/// checkbox is there to be pointed at, and a terminal that cannot say Ctrl-M is
/// no reason to hide one.
constexpr Attribute kAttributes[] = {
    {"Private", 'p', domain::attr::kPrivate},
    {"Crash", 'c', domain::attr::kCrash},
    {"Received", 'r', domain::attr::kRead},
    {"Sent", 's', domain::attr::kSent},
    {"File Attach", 'a', domain::attr::kFile},
    {"Transit", 'j', domain::attr::kInTransit},
    {"Orphan", 'o', domain::attr::kOrphan},
    {"Kill/Sent", 'k', domain::attr::kKillSent},
    {"Local", 'l', domain::attr::kLocal},
    {"Hold", 'h', domain::attr::kHold},
    {"File Request", 'f', domain::attr::kFileRequest},
    {"Return Rcpt Request", 'm', domain::attr::kReceiptRequest},
    {"Return Rcpt", 'n', domain::attr::kIsReceipt},
    {"Audit Request", 't', domain::attr::kAuditRequest},
    {"File Update Request", 'u', domain::attr::kUpdateRequest},
    {"Direct", 'd', domain::attr::kDirect},
    {"Immediate", 'i', domain::attr::kImmediate},
};
constexpr int kAttributeCount =
    static_cast<int>(sizeof(kAttributes) / sizeof(kAttributes[0]));

/// The letter that clears every attribute at once, GoldED's "Zap all attribs".
constexpr char kZapKey = 'z';

/// "Return Rcpt Request" and "File Update Request" are the longest names, at
/// nineteen columns; the column is that wide so that the chords line up under
/// one another whatever is above them.
constexpr int kNameWidth = 19;
/// "[x] " + the name + two spaces + "Ctrl-P".
constexpr int kCellWidth = 4 + kNameWidth + 2 + 6;
/// Between the two columns, and the margin the box keeps inside its frame.
constexpr int kGap = 2;
constexpr int kSideMargin = 1;
/// The frame itself, a column on each side.
constexpr int kFrame = 2;

/// Two columns where the window has room for them, one where it has not.
int columnsFor(int width) {
    return width >= (2 * kCellWidth) + kGap + (2 * kSideMargin) + kFrame ? 2 : 1;
}

/// How many rows a column of attributes stands on.
int linesFor(int columns) {
    return (kAttributeCount + columns - 1) / columns;
}

/// One checkbox: whether the attribute is set, what it is called and the chord
/// that turns it over.
///
/// The box in front is what says which attributes the message carries — colour
/// alone would leave a monochrome terminal with a list that says nothing — and
/// the set ones are written in the header's own colour besides. The one under
/// the cursor takes the fill the lists give their current row, so that Space is
/// plainly about it.
Element checkbox(const Attribute& attribute, uint32_t attributes, bool current) {
    const bool on = (attributes & attribute.bit) != 0;
    const std::string label = std::string(on ? "[x] " : "[ ] ") +
                              padRight(attribute.name, kNameWidth) + "  Ctrl-" +
                              static_cast<char>(attribute.key - 'a' + 'A');

    Element cell = text(padRight(label, kCellWidth));
    if (current) {
        return std::move(cell) | bold | color(theme::palette.selectionText) |
               bgcolor(theme::palette.selection);
    }
    if (on) return std::move(cell) | bold | color(theme::palette.dialogLabel);
    return std::move(cell) | color(theme::palette.dialogHint);
}

/// What the button that closes the dialog says, and how wide that leaves it —
/// measured rather than counted, so the two cannot drift apart.
constexpr const char* kDoneLabel = "  Done  ";

/// The button itself. Drawn selected, as the only thing Enter could mean here,
/// and lit for the length of a click on it.
Element doneButton(bool pressed) {
    auto label = text(kDoneLabel);
    if (pressed) label = std::move(label) | color(theme::palette.dialogFlash);
    return std::move(label) | bold | color(theme::palette.selectionText) |
           bgcolor(theme::palette.selection);
}

/// The top of the frame, with the title in the middle of it — the same bar the
/// area dialog is drawn with.
Element titleBar(const std::string& label, int width) {
    const std::string shown = truncateToWidth(label, width);
    const int left = std::max(0, (width - displayWidth(shown)) / 2);
    const int right = std::max(0, width - left - displayWidth(shown));
    return hbox({text("╭" + horizontalRule(left)) | color(theme::palette.dialogBorder),
                 text(shown) | color(theme::palette.dialogTitle),
                 text(horizontalRule(right) + "╮") | color(theme::palette.dialogBorder)});
}

/// One row of the box: the sides, and `content` between them.
Element framed(Element content) {
    const auto side = [] { return text("│") | color(theme::palette.dialogBorder); };
    return hbox({side(), std::move(content), side()});
}

/// A line of the box's own width, centred in it.
Element centred(const std::string& line, int inner, theme::Color tint) {
    const std::string shown = truncateToWidth(line, inner);
    const int left = std::max(0, (inner - displayWidth(shown)) / 2);
    return framed(hbox(
        {text(std::string(static_cast<size_t>(left), ' ')), text(shown) | color(tint),
         text(std::string(
             static_cast<size_t>(std::max(0, inner - left - displayWidth(shown))),
             ' '))}));
}

void toggle(AppState& state, int index) {
    if (index < 0 || index >= kAttributeCount) return;
    state.compose.attributes ^= kAttributes[static_cast<size_t>(index)].bit;
}

}  // namespace

void open(AppState& state) {
    AppState::AttributePicker picker;
    picker.before = state.compose.attributes;
    picker.boxes.assign(kAttributeCount, Box::Nowhere());
    state.attributePicker = std::move(picker);
}

Element render(AppState& state, Element background) {
    AppState::AttributePicker& picker = *state.attributePicker;
    picker.cursor = std::clamp(picker.cursor, 0, kAttributeCount - 1);

    const int columns = columnsFor(state.width);
    const int lines = linesFor(columns);
    const int inner = (columns * kCellWidth) + ((columns - 1) * kGap) + (2 * kSideMargin);

    // The room is reserved before anything is reflected into it: the boxes are
    // written while the frame is laid out, and a vector that grew under them
    // would leave the earlier rows pointing at freed memory.
    picker.boxes.assign(kAttributeCount, Box::Nowhere());

    Elements rows{titleBar(" Message attributes ", inner)};
    for (int line = 0; line < lines; ++line) {
        Elements cells{text(std::string(kSideMargin, ' '))};
        for (int column = 0; column < columns; ++column) {
            // Down the first column and then down the second, which is why the
            // index counts by whole columns rather than by rows.
            const int index = (column * lines) + line;
            if (column > 0) cells.push_back(text(std::string(kGap, ' ')));
            if (index >= kAttributeCount) {
                cells.push_back(text(std::string(kCellWidth, ' ')));
                continue;
            }
            cells.push_back(checkbox(kAttributes[static_cast<size_t>(index)],
                                     state.compose.attributes, index == picker.cursor) |
                            reflect(picker.boxes[static_cast<size_t>(index)]));
        }
        cells.push_back(text(std::string(kSideMargin, ' ')));
        rows.push_back(framed(hbox(std::move(cells))));
    }

    rows.push_back(centred("", inner, theme::palette.dialogText));
    const Element button =
        doneButton(state.isPressed(AppState::Pressed::AttributesDone)) |
        reflect(picker.doneBox);
    const int buttonWidth = displayWidth(kDoneLabel);
    const int left = std::max(0, (inner - buttonWidth) / 2);
    rows.push_back(framed(
        hbox({text(std::string(static_cast<size_t>(left), ' ')), button,
              text(std::string(
                  static_cast<size_t>(std::max(0, inner - left - buttonWidth)), ' '))})));
    rows.push_back(centred("space toggle · ctrl-z clear · enter done · esc cancel",
                           inner, theme::palette.dialogHint));
    rows.push_back(text("╰" + horizontalRule(inner) + "╯") |
                   color(theme::palette.dialogBorder));

    // dialog::surface() wipes the screen behind the box and lays the dialog's
    // own fill down in its place, so the header underneath neither shows
    // through it nor colors it.
    return dbox({std::move(background), dialog::surface(vbox(std::move(rows))) | center});
}

void handleEvent(AppState& state, const Event& event) {
    AppState::AttributePicker& picker = *state.attributePicker;
    const int columns = columnsFor(state.width);
    const int lines = linesFor(columns);

    // A click turns over the attribute it landed on, without moving the cursor
    // there first: pointing at a checkbox and pressing is one gesture, not two.
    // The cursor follows it all the same, so that Space carries on from where
    // the pointer left off.
    if (const auto click = leftClick(event)) {
        for (int i = 0; i < kAttributeCount; ++i) {
            if (!picker.boxes[static_cast<size_t>(i)].Contain(click->x, click->y)) {
                continue;
            }
            picker.cursor = i;
            toggle(state, i);
            return;
        }
        if (picker.doneBox.Contain(click->x, click->y)) {
            state.showClick(AppState::Pressed::AttributesDone);
            state.attributePicker.reset();
        }
        // Anywhere else, inside the box or outside it: swallowed, as every
        // other event is while the dialog is modal.
        return;
    }

    // The chords, which is what the list of them beside the names is for. Every
    // one of them but `app.quit` is the dialog's while it is up — the shell
    // hands it the key ahead of every other binding, so Ctrl-C is Crash here
    // whatever a layout has made of it elsewhere.
    if (event.ctrl()) {
        for (int i = 0; i < kAttributeCount; ++i) {
            if (isCtrl(event, kAttributes[static_cast<size_t>(i)].key)) {
                picker.cursor = i;
                toggle(state, i);
                return;
            }
        }
        if (isCtrl(event, kZapKey)) state.compose.attributes = 0;
        return;
    }

    if (event == Event::Character(' ')) {
        toggle(state, picker.cursor);
        return;
    }
    // Down and up walk the column the cursor is in, left and right step between
    // the columns — the movement the layout reads as, rather than the order the
    // attributes are numbered in.
    if (event == Event::ArrowDown && (picker.cursor % lines) + 1 < lines &&
        picker.cursor + 1 < kAttributeCount) {
        ++picker.cursor;
        return;
    }
    if (event == Event::ArrowUp && (picker.cursor % lines) > 0) {
        --picker.cursor;
        return;
    }
    if ((event == Event::ArrowRight || event == Event::Tab) &&
        picker.cursor + lines < kAttributeCount) {
        picker.cursor += lines;
        return;
    }
    if ((event == Event::ArrowLeft || event == Event::TabReverse) &&
        picker.cursor >= lines) {
        picker.cursor -= lines;
        return;
    }
    if (event == Event::Return) {
        state.attributePicker.reset();
        return;
    }
    if (event == Event::Escape) {
        // Every toggle landed on the message as it was made, so putting it back
        // is what Esc has to mean: the dialog has no copy of its own to drop.
        state.compose.attributes = picker.before;
        state.attributePicker.reset();
    }
}

}  // namespace amberedit::ui::attributes_dialog
