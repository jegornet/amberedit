#include "ui/line_edit.hpp"

#include <algorithm>

#include "ui/text_editor.hpp"

namespace amberedit::ui::line_edit {

using term::Event;

FieldSelection selection(const std::string& value, size_t cursor,
                         const std::optional<size_t>& anchor) {
    if (!anchor) return {};
    const size_t at = std::min(*anchor, value.size());
    const size_t to = std::min(cursor, value.size());
    return {std::min(at, to), std::max(at, to)};
}

bool extendsSelection(const Event& event) {
    if (!event.shift() || event.ctrl() || event.alt() ||
        event.kind() != Event::Kind::Key) {
        return false;
    }
    switch (event.name()) {
        case Event::Name::ArrowLeft:
        case Event::Name::ArrowRight:
        case Event::Name::ArrowUp:
        case Event::Name::ArrowDown:
        case Event::Name::Home:
        case Event::Name::End: return true;
        default: return false;
    }
}

bool typesText(const Event& event) {
    if (!event.is_character() || event.ctrl() || event.alt()) return false;
    const std::string& input = event.character();
    return !input.empty() &&
           !(input.size() == 1 && static_cast<unsigned char>(input[0]) < 0x20);
}

void extend(Line line, const Event& event) {
    if (!line.anchor) line.anchor = line.cursor;
    switch (event.name()) {
        case Event::Name::ArrowLeft:
            line.cursor = prevChar(line.value, line.cursor);
            break;
        case Event::Name::ArrowRight:
            line.cursor = std::min(line.value.size(),
                                   line.cursor + charLen(line.value, line.cursor));
            break;
        case Event::Name::ArrowUp:
        case Event::Name::Home: line.cursor = 0; break;
        default: line.cursor = line.value.size(); break;
    }
}

std::string selectedText(const Line& line) {
    const FieldSelection chosen = selection(line.value, line.cursor, line.anchor);
    return line.value.substr(chosen.begin, chosen.end - chosen.begin);
}

bool deleteSelected(Line line) {
    const FieldSelection chosen = selection(line.value, line.cursor, line.anchor);
    line.anchor.reset();
    if (chosen.empty()) return false;
    line.value.erase(chosen.begin, chosen.end - chosen.begin);
    line.cursor = chosen.begin;
    return true;
}

bool editKey(Line line, const Event& event, const KeyMap& keys) {
    if (extendsSelection(event)) {
        extend(line, event);
        return true;
    }
    std::string& value = line.value;
    size_t& cursor = line.cursor;
    cursor = std::min(cursor, value.size());

    const bool overSelection =
        !selection(value, cursor, line.anchor).empty() &&
        (event == Event::Backspace || event == Event::Delete || typesText(event));
    if (overSelection) {
        deleteSelected(line);
        if (event == Event::Backspace || event == Event::Delete) return true;
    }
    line.anchor.reset();

    // Before the arrows: Alt-Left is a left arrow with a modifier on it. A field
    // holds one line, so a motion with no word that way stops at the edge of it
    // rather than walking on.
    if (keys.is(event, Command::ComposeWordLeft)) {
        cursor = wordStartBefore(value, cursor).value_or(0);
        return true;
    }
    if (keys.is(event, Command::ComposeWordRight)) {
        cursor = wordEndAfter(value, cursor).value_or(value.size());
        return true;
    }
    if (keys.is(event, Command::ComposeDeleteWord)) {
        const size_t from = wordStartBefore(value, cursor).value_or(0);
        value.erase(from, cursor - from);
        cursor = from;
        return true;
    }
    if (keys.is(event, Command::ComposeDeleteNextWord)) {
        const size_t to = wordEndAfter(value, cursor).value_or(value.size());
        value.erase(cursor, to - cursor);
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
    // The ends of the field, from the keys marked for them and from the chords
    // a terminal that sends nothing for Home and End leaves as the only way
    // there.
    if (event == Event::Home || keys.is(event, Command::ComposeLineStart)) {
        cursor = 0;
        return true;
    }
    if (event == Event::End || keys.is(event, Command::ComposeLineEnd)) {
        cursor = value.size();
        return true;
    }
    if (event == Event::Backspace) {
        const size_t from = prevChar(value, cursor);
        value.erase(from, cursor - from);
        cursor = from;
        return true;
    }
    if (event == Event::Delete) {
        value.erase(cursor, charLen(value, cursor));
        return true;
    }
    // A chord arrives as a character with Ctrl or Alt held, and one nothing
    // above binds must not be typed as its letter.
    if (!typesText(event)) return false;
    const std::string& input = event.character();
    // A full field takes nothing more, and the keystroke is swallowed rather
    // than passed on: it was aimed at this field.
    if (line.limit != 0 && charCount(value) + charCount(input) > line.limit) return true;
    value.insert(cursor, input);
    cursor += input.size();
    return true;
}

void insert(Line line, std::string_view text) {
    deleteSelected(line);
    const std::string_view first = text.substr(0, text.find('\n'));
    const std::string piece(first);
    size_t take = piece.size();
    if (line.limit != 0) {
        const size_t used = charCount(line.value);
        const size_t room = line.limit > used ? line.limit - used : 0;
        take = 0;
        for (size_t n = 0; take < piece.size() && n < room; ++n) {
            take += charLen(piece, take);
        }
    }
    line.cursor = std::min(line.cursor, line.value.size());
    line.value.insert(line.cursor, piece, 0, take);
    line.cursor += take;
}

}  // namespace amberedit::ui::line_edit
