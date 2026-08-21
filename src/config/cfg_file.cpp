#include "config/cfg_file.hpp"

#include <charconv>
#include <stdexcept>
#include <string>

#include "config/text_util.hpp"

namespace amberedit::config {
namespace {

/// Splits one line into its words. A double quote may open anywhere and runs to
/// the next one, so `-d "hello world"` is one value and `""` is an empty one;
/// a `#` starting a word ends the line. Inside quotes both characters are
/// ordinary text, which is how a value may contain either.
std::vector<std::string> tokenize(std::string_view line, const std::string& origin,
                                  int lineNumber) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && text::asciiIsSpace(line[i])) ++i;
        if (i >= line.size() || line[i] == '#') break;

        std::string token;
        while (i < line.size() && !text::asciiIsSpace(line[i])) {
            if (line[i] != '"') {
                token += line[i++];
                continue;
            }
            const size_t end = line.find('"', ++i);
            if (end == std::string_view::npos) {
                throw std::runtime_error(origin + ":" + std::to_string(lineNumber) +
                                         ": a quoted value is never closed");
            }
            token.append(line, i, end - i);
            i = end + 1;
        }
        tokens.push_back(std::move(token));
    }
    return tokens;
}

}  // namespace

void CfgEntry::fail(std::string_view what) const {
    throw std::runtime_error(origin + ":" + std::to_string(line) + ": " +
                             std::string(what));
}

std::string CfgEntry::text() const {
    if (values.empty()) fail(key + " needs a value");

    std::string joined;
    for (const auto& value : values) {
        if (!joined.empty()) joined += ' ';
        joined += value;
    }
    return joined;
}

const std::string& CfgEntry::one() const {
    if (values.size() != 1) fail(key + " takes exactly one value");
    return values.front();
}

long long CfgEntry::number() const {
    const std::string& value = one();

    long long parsed = 0;
    const char* end = value.data() + value.size();
    const auto [stopped, ec] = std::from_chars(value.data(), end, parsed);
    if (ec != std::errc{} || stopped != end) {
        fail(key + " must be a whole number, not '" + value + "'");
    }
    return parsed;
}

long long CfgEntry::numberIn(long long min, long long max) const {
    const long long value = number();
    if (value < min || value > max) {
        fail(key + " must be between " + std::to_string(min) + " and " +
             std::to_string(max) + ", got " + std::to_string(value));
    }
    return value;
}

bool CfgEntry::flag() const {
    const std::string& value = one();
    if (text::iequals(value, "on")) return true;
    if (text::iequals(value, "off")) return false;
    fail(key + " must be on or off, not '" + value + "'");
}

std::vector<CfgEntry> parseCfg(std::string_view text, const std::string& originName) {
    std::vector<CfgEntry> entries;

    int lineNumber = 0;
    for (const auto& line : text::splitLines(text)) {
        ++lineNumber;
        auto tokens = tokenize(line, originName, lineNumber);
        if (tokens.empty()) continue;

        CfgEntry entry;
        entry.key = text::toLower(tokens.front());
        entry.values.assign(std::make_move_iterator(tokens.begin() + 1),
                            std::make_move_iterator(tokens.end()));
        entry.origin = originName;
        entry.line = lineNumber;

        if (entry.key.empty()) entry.fail("a line starts with an empty value");

        // The two shapes a file written for the toml AmberEdit used to read still
        // has in it. Both would otherwise pass for a key with odd values —
        // `text = 33` for a key `text` whose value is "= 33" — and the setting
        // would go wrong somewhere further along instead of here.
        if (entry.key.front() == '[') {
            entry.fail(
                "a [section] header — an AmberEdit config is a flat list of "
                "'key value' lines now, with no sections");
        }
        if (!entry.values.empty() && entry.values.front() == "=") {
            entry.fail("'" + entry.key +
                       " = value' is the old toml spelling; write '" + entry.key +
                       " value'");
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

}  // namespace amberedit::config
