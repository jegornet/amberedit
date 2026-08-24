#include "config/list_format.hpp"

#include <algorithm>
#include <string>

#include "config/text_util.hpp"

namespace amberedit::config {
namespace {

/// Wider than any terminal anyone has, and narrow enough that a width typed
/// with a digit too many is caught here rather than swallowing the row.
constexpr int kMaxFieldWidth = 255;

std::string quoted(std::string_view format) {
    return "\"" + std::string(format) + "\"";
}

/// What a complaint about brackets after the wrong letter says next: which
/// letters do take a format, or that none of them does. A list whose fields are
/// all drawn from what they hold has nothing to offer instead, and saying so is
/// shorter than leaving the user to work it out.
std::string takesInstead(const ListFormatSpec& spec) {
    if (spec.formatted.empty()) return ", and no field of this list is";
    return " — the ones that are: " + std::string(spec.formatted);
}

}  // namespace

tl::expected<ListFormatRow, ErrorPtr> parseListFormat(const CfgEntry& entry,
                                                      const ListFormatSpec& spec,
                                                      const std::string& value) {
    const std::string setting(spec.setting);
    ListFormatRow lines{ListFormatLine{}};

    for (size_t i = 0; i < value.size();) {
        if (value[i] == ' ') {
            lines.back().push_back(ListFormatField{' ', 1});
            ++i;
            continue;
        }
        if (value[i] == '\\' && i + 1 < value.size() &&
            text::asciiLower(value[i + 1]) == 'n') {
            if (static_cast<int>(lines.size()) >= kMaxFormatLines) {
                return entry.fail(setting + ": a row is asked for more than " +
                                  std::to_string(kMaxFormatLines) + " lines");
            }
            lines.emplace_back();
            i += 2;
            continue;
        }

        const char letter = text::asciiLower(value[i]);
        const auto found = std::find_if(
            spec.letters.begin(), spec.letters.end(),
            [letter](const ListFormatLetter& name) { return name.letter == letter; });
        if (found == spec.letters.end()) {
            return entry.fail(setting + ": '" + std::string(1, value[i]) +
                              "' is not a field (" + std::string(spec.fields) +
                              "), a width is written after the letter it belongs to, and "
                              "\\n begins the next line of the row");
        }
        ++i;

        // The digits after a letter are that field's width; with none it stands
        // as wide as the letter's own default, which for some is a width worked
        // out from what the field holds rather than a number at all.
        int width = found->width;
        if (i < value.size() && value[i] >= '0' && value[i] <= '9') {
            width = 0;
            while (i < value.size() && value[i] >= '0' && value[i] <= '9') {
                width = (width * 10) + (value[i] - '0');
                if (width > kMaxFieldWidth) {
                    return entry.fail(setting + ": '" + std::string(1, letter) +
                                      "' is asked for more than " +
                                      std::to_string(kMaxFieldWidth) + " columns");
                }
                ++i;
            }
        }
        // A format of its own, in brackets after the width. Everything up to the
        // first `)` belongs to it, spaces included: the whole value is one
        // quoted string, so a space in there is no more trouble than the space
        // between two fields.
        std::string format;
        if (i < value.size() && value[i] == '(') {
            if (!found->takesFormat) {
                return entry.fail(setting + ": '" + std::string(1, letter) +
                                  "' is not written by a format of its own" +
                                  takesInstead(spec));
            }
            const size_t close = value.find(')', i);
            if (close == std::string::npos) {
                return entry.fail(
                    setting + ": the format after '" + std::string(1, letter) +
                    "' is never closed — a ')' ends it, e.g. " + quoted(spec.example));
            }
            format = value.substr(i + 1, close - i - 1);
            if (format.empty()) {
                return entry.fail(
                    setting + ": '" + std::string(1, letter) +
                    "()' asks for a format and names none — the brackets are "
                    "left off where the field is to be written as ever");
            }
            i = close + 1;
            // The width belongs in front of the format, and digits after the
            // brackets would otherwise be read as a field nobody wrote.
            if (i < value.size() && value[i] >= '0' && value[i] <= '9') {
                return entry.fail(setting +
                                  ": a width goes before the format it belongs to — '" +
                                  std::string(1, letter) + "15(" + format + ")'");
            }
        }

        lines.back().push_back(ListFormatField{letter, width, std::move(format)});
    }

    // Every line has to hold something. A row that is to have a blank line in it
    // gets one from a line holding a space — that is a line asked for, where an
    // empty one is a `\n` written once too often.
    for (const auto& line : lines) {
        if (!line.empty()) continue;
        if (lines.size() == 1) {
            std::string what = setting;
            what += ": the fields are missing, e.g. ";
            what += setting;
            what += " ";
            what += quoted(spec.example);
            return entry.fail(what);
        }
        std::string what = setting;
        what += ": a line of the format holds no fields — a blank line in a row ";
        what += "is written as a space";
        return entry.fail(what);
    }
    return lines;
}

tl::expected<ListFormats, ErrorPtr> parseListFormats(const CfgEntry& entry,
                                                     const ListFormatSpec& spec) {
    const std::string setting(spec.setting);
    if (entry.values.empty()) {
        return entry.fail(setting + " needs the fields to show, e.g. " + setting + " " +
                          quoted(spec.example) +
                          " — there is no way to ask for an empty row");
    }
    if (entry.values.size() > 2) {
        return entry.fail(
            setting + " takes one format, or two — the narrow window's and the wide " +
            "one's; a format with a space in it is written in quotes, e.g. " + setting +
            " " + quoted(spec.example) + " " + quoted(spec.wideExample));
    }

    auto narrow = parseListFormat(entry, spec, entry.values.front());
    if (!narrow) return tl::make_unexpected(std::move(narrow).error());

    ListFormats formats;
    formats.narrow = *narrow;
    if (entry.values.size() != 2) {
        formats.wide = formats.narrow;
        return formats;
    }
    auto wide = parseListFormat(entry, spec, entry.values.back());
    if (!wide) return tl::make_unexpected(std::move(wide).error());
    formats.wide = *wide;
    return formats;
}

tl::expected<ListFormatRow, ErrorPtr> parseOneListFormat(const CfgEntry& entry,
                                                         const ListFormatSpec& spec) {
    const std::string setting(spec.setting);
    if (entry.values.empty()) {
        return entry.fail(setting + " needs the fields to show, e.g. " + setting + " " +
                          quoted(spec.example) +
                          " — there is no way to ask for an empty row");
    }
    if (entry.values.size() > 1) {
        return entry.fail(setting + " takes one format and not two — what it lays out " +
                          "is only ever on the screen in a wide window; a format with " +
                          "a space in it is written in quotes, e.g. " + setting + " " +
                          quoted(spec.example));
    }
    return parseListFormat(entry, spec, entry.values.front());
}

}  // namespace amberedit::config
