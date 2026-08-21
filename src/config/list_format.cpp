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

}  // namespace

Result<ListFormatRow> parseListFormat(const CfgEntry& entry, const ListFormatSpec& spec,
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
        lines.back().push_back(ListFormatField{letter, width});
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

Result<ListFormats> parseListFormats(const CfgEntry& entry, const ListFormatSpec& spec) {
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

    const auto narrow = parseListFormat(entry, spec, entry.values.front());
    if (!narrow) return tl::make_unexpected(narrow.error());

    ListFormats formats;
    formats.narrow = *narrow;
    if (entry.values.size() != 2) {
        formats.wide = formats.narrow;
        return formats;
    }
    const auto wide = parseListFormat(entry, spec, entry.values.back());
    if (!wide) return tl::make_unexpected(wide.error());
    formats.wide = *wide;
    return formats;
}

}  // namespace amberedit::config
