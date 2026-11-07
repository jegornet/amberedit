#include "ui/text_field.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "app/clipboard.hpp"
#include "ui/event_util.hpp"
#include "ui/line_edit.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui {

using namespace term;

namespace {

line_edit::Line lineOf(TextField& field, size_t limit) {
    return {field.value, field.cursor, field.anchor, limit};
}

/// What is selected, to wherever the context sends a copy; cut out of the
/// field where `cut` says so.
void copySelected(TextField& field, const FieldContext& context, bool cut, bool quiet) {
    std::string text = line_edit::selectedText(lineOf(field, context.limit));
    if (text.empty()) return;
    if (cut) {
        line_edit::deleteSelected(lineOf(field, context.limit));
        field.touched = true;
    }
    if (context.copy) context.copy(std::move(text), quiet);
}

/// The byte under column `x`, held to the field: a drag carried past either end
/// of it means that end, and a character over, which is what scrolls a value
/// wider than the box along under the pointer.
size_t byteAt(const TextField& field, int x) {
    const int column =
        std::clamp(x, field.box.x_min, field.box.x_max + 1) - field.box.x_min;
    return offsetAtColumn(field.value, field.origin, column);
}

void dragTo(TextField& field, int x) {
    field.cursor = byteAt(field, x);
    if (field.held) {
        const auto [anchor, cursor] = field.held->endsFor(field.cursor);
        field.anchor = anchor;
        field.cursor = cursor;
    }
}

void endDrag(TextField& field) {
    field.dragging = false;
    field.held.reset();
}

/// A press on the field, the editor's way: the cursor where it landed and the
/// anchor beside it for a drag to carry the cursor away from — or, the second
/// and third press of a run, the word under it and then the whole.
void press(TextField& field, const MouseEvent& click, const FieldContext& context) {
    const FieldSelection before = fieldSelection(field);
    field.cursor = byteAt(field, click.x);
    endDrag(field);

    const int count = countClick(field.clicks, click.x, click.y, context.now);
    if (count > 1) {
        const auto [begin, end] = count == 2
                                      ? wordAround(field.value, field.cursor)
                                      : std::pair<size_t, size_t>{0, field.value.size()};
        field.anchor = begin;
        field.cursor = end;
        if (context.copyOnSelect != config::CopyOnSelect::Off) {
            copySelected(field, context, false, /*quiet=*/true);
        }
        return;
    }
    field.anchor = field.cursor;
    field.dragging = true;
    using Held = HeldSelection<size_t>;
    if (!before.empty() && Held::covers(before.begin, before.end, field.cursor)) {
        field.held = Held{before.begin, before.end, field.cursor};
    }
}

}  // namespace

void setFieldValue(TextField& field, std::string value) {
    field.value = std::move(value);
    field.cursor = field.value.size();
    field.anchor.reset();
    endDrag(field);
    field.touched = false;
}

void selectAllOf(TextField& field) {
    field.cursor = field.value.size();
    field.anchor.reset();
    if (!field.value.empty()) field.anchor = 0;
}

FieldSelection fieldSelection(const TextField& field) {
    return line_edit::selection(field.value, field.cursor, field.anchor);
}

bool handleFieldKey(TextField& field, const Event& event, const FieldContext& context) {
    if (context.keys.is(event, Command::ComposeCopy)) {
        copySelected(field, context, false, /*quiet=*/false);
        return true;
    }
    if (context.keys.is(event, Command::ComposeCut)) {
        copySelected(field, context, true, /*quiet=*/false);
        return true;
    }
    if (context.keys.is(event, Command::ComposePaste)) {
        if (context.paste) {
            if (const auto text = context.paste())
                pasteIntoField(field, *text, context.limit);
        }
        return true;
    }
    if (line_edit::extendsSelection(event)) {
        line_edit::extend(lineOf(field, context.limit), event);
        if (context.copyOnSelect == config::CopyOnSelect::Always) {
            copySelected(field, context, false, /*quiet=*/true);
        }
        return true;
    }
    // ↑ and ↓ walk the box the field stands in, and leave nothing selected
    // behind them in it.
    if (event == Event::ArrowUp || event == Event::ArrowDown) {
        field.anchor.reset();
        return false;
    }
    const std::string before = field.value;
    if (!line_edit::editKey(lineOf(field, context.limit), event, context.keys)) {
        return false;
    }
    if (field.value != before) field.touched = true;
    return true;
}

bool handleFieldMouse(TextField& field, const Event& event, const FieldContext& context) {
    const auto mouse = mouseOf(event);
    if (!mouse || mouse->button != MouseEvent::Button::Left) return false;

    if (field.dragging) {
        switch (mouse->motion) {
            case MouseEvent::Motion::Moved: dragTo(field, mouse->x); return true;
            case MouseEvent::Motion::Released:
                dragTo(field, mouse->x);
                endDrag(field);
                if (context.copyOnSelect != config::CopyOnSelect::Off) {
                    copySelected(field, context, false, /*quiet=*/true);
                }
                return true;
            case MouseEvent::Motion::Pressed:
                // A press that stands for a release nobody reported carries the
                // drag on where it lands on the field, and is a click anywhere
                // else.
                if (mouse->resumed && field.box.Contain(mouse->x, mouse->y)) {
                    dragTo(field, mouse->x);
                    return true;
                }
                endDrag(field);
                break;
        }
    }
    if (mouse->motion != MouseEvent::Motion::Pressed) return false;
    if (!field.box.Contain(mouse->x, mouse->y)) return false;
    // offsetAtColumn() is measured against the scroll inputField() settled on,
    // so a click never lands inside a UTF-8 sequence.
    press(field, *mouse, context);
    return true;
}

void pasteIntoField(TextField& field, std::string_view text, size_t limit) {
    const std::vector<std::string> lines = app::pastedLines(text);
    if (lines.empty()) return;
    line_edit::insert(lineOf(field, limit), lines.front());
    field.touched = true;
}

Element renderField(TextField& field, int width, bool active) {
    field.cursor = std::min(field.cursor, field.value.size());
    field.box = Box::Nowhere();
    return inputField(
               field.value, field.cursor, width, active,
               active ? theme::palette.dialogFocusedText : theme::palette.dialogLabel,
               fieldFiller(theme::palette.dialogHint), &field.origin,
               active ? fieldSelection(field) : FieldSelection{}) |
           bgcolor(active ? theme::palette.dialogFocusedField
                          : theme::palette.dialogField) |
           reflect(field.box);
}

}  // namespace amberedit::ui
