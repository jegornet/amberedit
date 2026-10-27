#include "ui/input_field.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ui/text_layout.hpp"

namespace amberedit::ui {

using namespace term;

size_t prevChar(const std::string& text, size_t at) {
    if (at == 0) return 0;
    size_t i = at - 1;
    while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0u) == 0x80u) --i;
    return i;
}

size_t charLen(const std::string& text, size_t at) {
    if (at >= text.size()) return 0;
    size_t end = at + 1;
    while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0u) == 0x80u)
        ++end;
    return end - at;
}

size_t charCount(std::string_view text) {
    size_t count = 0;
    for (const char byte : text) {
        if ((static_cast<unsigned char>(byte) & 0xC0u) != 0x80u) ++count;
    }
    return count;
}

size_t offsetAtColumn(const std::string& value, size_t origin, int column) {
    if (origin >= value.size()) return value.size();
    return origin + substrByWidth(value.substr(origin), 0, std::max(0, column)).size();
}

namespace {

/// The columns past the end of what is written: underscores where the field was
/// given a `filler` color to draw them in, blanks where it was not.
Element room(int columns, std::optional<theme::Color> filler) {
    if (columns <= 0) return text("");
    const std::string run(static_cast<size_t>(columns), filler ? '_' : ' ');
    if (!filler) return text(run);
    return text(run) | color(*filler);
}

/// Selected text, as every bar in the interface is drawn.
Element selected(std::string run) {
    return theme::selectionBold(text(std::move(run)) | color(theme::palette.selectionText) |
                                bgcolor(theme::palette.selection));
}

/// The columns past the end of what is written, every one of them selected
/// where the selection runs past the end of the text.
Element roomAfter(int columns, std::optional<theme::Color> filler, bool allSelected) {
    if (!allSelected || columns <= 0) return room(columns, filler);
    return selected(std::string(static_cast<size_t>(columns), ' '));
}

}  // namespace

Element selectedRun(const std::string& piece, size_t from, theme::Color tint,
                    FieldSelection selection) {
    const size_t to = from + piece.size();
    if (selection.empty() || selection.end <= from || selection.begin >= to) {
        return text(piece) | color(tint);
    }
    const size_t begin = std::max(selection.begin, from) - from;
    const size_t end = std::min(selection.end, to) - from;
    Elements runs;
    if (begin > 0) runs.push_back(text(piece.substr(0, begin)) | color(tint));
    runs.push_back(selected(piece.substr(begin, end - begin)));
    if (end < piece.size()) runs.push_back(text(piece.substr(end)) | color(tint));
    return runs.size() == 1 ? std::move(runs.front()) : hbox(std::move(runs));
}

std::optional<theme::Color> fieldFiller(theme::Color color) {
    if (!theme::palette.inputFillerShown) return std::nullopt;
    return color;
}

Element inputField(const std::string& value, size_t cursor, int width, bool active,
                   theme::Color tint, std::optional<theme::Color> filler,
                   size_t* origin, FieldSelection selection) {
    // Never past the end of what is there. A field whose text has just been
    // replaced under a cursor left where it was is the way this happens, and a
    // frame is the wrong place to find out about it.
    cursor = std::min(cursor, value.size());

    const auto drawn = [origin](Element element, size_t at) {
        if (origin != nullptr) *origin = at;
        return element;
    };

    // Whether what follows the text is selected — a row of the message whose
    // line break is — and so drawn as selected out to the edge of the field,
    // the cursor's own cell excepted.
    const bool pastEnd = !selection.empty() && selection.end > value.size();

    if (!active) {
        const std::string shown = truncateToWidth(value, width);
        return drawn(hbox({selectedRun(shown, 0, tint, selection),
                           roomAfter(width - displayWidth(shown), filler,
                                     pastEnd && shown.size() == value.size())}),
                     0);
    }

    const size_t atLen = charLen(value, cursor);
    std::string left = value.substr(0, cursor);
    // A space stands in for the character under the cursor at the end of the
    // text, so the cursor is visible in an empty field too.
    const std::string at = atLen > 0 ? value.substr(cursor, atLen) : " ";
    std::string right = value.substr(cursor + atLen);

    const int atWidth = std::max(1, displayWidth(at));
    int leftWidth = displayWidth(left);
    if (leftWidth + atWidth > width) {
        left = substrByWidth(left, leftWidth + atWidth - width, width);
        leftWidth = displayWidth(left);
    }
    right = substrByWidth(right, 0, std::max(0, width - leftWidth - atWidth));

    const int used = leftWidth + atWidth + displayWidth(right);
    // What is drawn is `value` from where the scroll left off — `left` is a
    // suffix of everything before the cursor, so what it lost off its front is
    // where the field now begins.
    const size_t leftFrom = cursor - left.size();
    const size_t rightFrom = cursor + atLen;
    return drawn(hbox({selectedRun(left, leftFrom, tint, selection), text(at) | inverted,
                       selectedRun(right, rightFrom, tint, selection),
                       roomAfter(width - used, filler,
                                 pastEnd && rightFrom + right.size() == value.size())}),
                 leftFrom);
}

}  // namespace amberedit::ui
