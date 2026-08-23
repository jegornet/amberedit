#include "ui/text_field.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include "ui/input_field.hpp"

namespace amberedit::ui {

using namespace term;

namespace {

/// What an event types into a field, which is anything a value can hold: a name
/// is Cyrillic as often as not, and a path may hold a space.
std::optional<std::string> typedText(const Event& event) {
    if (!event.is_character() || event.ctrl() || event.alt()) return std::nullopt;
    const std::string& input = event.character();
    if (input.empty()) return std::nullopt;
    if (input.size() == 1 && static_cast<unsigned char>(input[0]) < 0x20) {
        return std::nullopt;
    }
    return input;
}

}  // namespace

void setFieldValue(TextField& field, std::string value) {
    field.value = std::move(value);
    field.cursor = field.value.size();
    field.touched = false;
}

bool handleFieldKey(TextField& field, const Event& event) {
    std::string& value = field.value;
    size_t& cursor = field.cursor;
    cursor = std::min(cursor, value.size());

    if (const auto typed = typedText(event)) {
        value.insert(cursor, *typed);
        cursor += typed->size();
        field.touched = true;
        return true;
    }
    if (event == Event::Backspace) {
        const size_t from = prevChar(value, cursor);
        value.erase(from, cursor - from);
        cursor = from;
        field.touched = true;
        return true;
    }
    if (event == Event::Delete) {
        value.erase(cursor, charLen(value, cursor));
        field.touched = true;
        return true;
    }
    if (event == Event::ArrowLeft) {
        cursor = prevChar(value, cursor);
        return true;
    }
    if (event == Event::ArrowRight) {
        cursor = std::min(value.size(), cursor + charLen(value, cursor));
        return true;
    }
    if (event == Event::Home) {
        cursor = 0;
        return true;
    }
    if (event == Event::End) {
        cursor = value.size();
        return true;
    }
    return false;
}

void clickField(TextField& field, int x) {
    // Against the origin the last frame wrote back, which is the only thing that
    // knows how far the text had scrolled when it was drawn.
    field.cursor = offsetAtColumn(field.value, field.origin, x - field.box.x_min);
}

Element renderField(TextField& field, int width, bool active) {
    field.cursor = std::min(field.cursor, field.value.size());
    field.box = Box::Nowhere();
    return inputField(field.value, field.cursor, width, active,
                      active ? theme::palette.selectionText : theme::palette.dialogLabel,
                      fieldFiller(theme::palette.dialogHint), &field.origin) |
           bgcolor(active ? theme::palette.selection : theme::palette.dialogField) |
           reflect(field.box);
}

}  // namespace amberedit::ui
