#include "ui/input_field.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

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

Element inputField(const std::string& value, size_t cursor, int width, bool active,
                   theme::Color tint, size_t* origin) {
    // Never past the end of what is there. A field whose text has just been
    // replaced under a cursor left where it was is the way this happens, and a
    // frame is the wrong place to find out about it.
    cursor = std::min(cursor, value.size());

    const auto drawn = [origin](Element element, size_t at) {
        if (origin != nullptr) *origin = at;
        return element;
    };

    if (!active) {
        return drawn(text(padRight(truncateToWidth(value, width), width)) | color(tint),
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
    return drawn(
        hbox({text(left) | color(tint), text(at) | inverted,
              text(right +
                   std::string(static_cast<size_t>(std::max(0, width - used)), ' ')) |
                  color(tint)}),
        cursor - left.size());
}

}  // namespace amberedit::ui
