#include "echolist/echolist_parser.hpp"

#include <cstddef>
#include <string>

#include "config/text_util.hpp"

namespace amberedit::echolist {
namespace {

/// The end-of-file mark DOS wrote after the last line. Optional, and most
/// echolists have none — but where one stands, everything from it on is not the
/// echolist.
constexpr char kDosEof = '\x1a';

/// How much of a line a warning quotes back. Enough to recognise it by, and
/// short enough that a file that is not an echolist at all does not fill the
/// screen with itself.
constexpr size_t kQuoted = 40;

/// The head of a line, for a warning to quote. The text has been decoded by the
/// time it gets here, so the cut is backed off to a character boundary: half a
/// UTF-8 sequence reaches the terminal as a replacement glyph, and a warning
/// about a mangled line should not itself be mangled.
std::string quoted(std::string_view line) {
    if (line.size() <= kQuoted) return std::string(line);
    // Back off over the continuation bytes the cut may have landed in the middle
    // of; the byte at `end` is the first one left out, so it is the one that
    // says whether anything is being cut in half.
    size_t end = kQuoted;
    while (end > 0 && (static_cast<unsigned char>(line[end]) & 0xC0) == 0x80) --end;
    return std::string(line.substr(0, end));
}

/// The part after the last dot, empty where the name has none.
std::string_view extensionOf(std::string_view name) {
    const size_t dot = name.find_last_of('.');
    return dot == std::string_view::npos ? std::string_view{} : name.substr(dot + 1);
}

/// One `.lst` line: `[Status],Tag,Comment,Moderator's Name,Address,`.
///
/// The commas are the whole of the structure — the format has no quoting and no
/// escape, so a comma is a separator wherever it stands and a description
/// written with one in it is a line whose author has cut their own description
/// short. Only the second and third fields are read; the status, the moderator
/// and their address are what an echolist says for other programs than this
/// one.
void readLstLine(std::string_view line, int lineNumber, ParseResult& result) {
    // The tag and the description are the first three fields; the rest of the
    // line is nothing here reads, however many commas it holds.
    std::vector<std::string_view> fields;
    std::string_view rest = line;
    while (fields.size() < 3) {
        const size_t comma = rest.find(',');
        if (comma == std::string_view::npos) {
            fields.push_back(rest);
            break;
        }
        fields.push_back(rest.substr(0, comma));
        rest = rest.substr(comma + 1);
    }

    if (fields.size() < 3) {
        result.warnings.push_back(
            {lineNumber, "not an echolist line: '" + quoted(line) +
                             "' — a line is status, tag, description, moderator "
                             "and address, separated by commas"});
        return;
    }

    const std::string_view tag = config::text::trim(fields[1]);
    if (tag.empty()) {
        result.warnings.push_back(
            {lineNumber, "the line names no echo: '" + quoted(line) + "'"});
        return;
    }

    const std::string_view description = config::text::trim(fields[2]);
    if (description.empty()) return;
    result.entries.push_back({std::string(tag), std::string(description)});
}

/// One `.na` line: the tag, blanks, and the description running to the end.
void readNaLine(std::string_view line, ParseResult& result) {
    size_t end = 0;
    while (end < line.size() && !config::text::asciiIsSpace(line[end])) ++end;

    const std::string_view tag = line.substr(0, end);
    const std::string_view description = config::text::trim(line.substr(end));
    // A tag with nothing after it is the whole line, and a line that is one word
    // says nothing about the echo it names — the same nothing an empty
    // description is, and passed over the same way.
    if (description.empty()) return;
    result.entries.push_back({std::string(tag), std::string(description)});
}

}  // namespace

EcholistFormat formatOf(std::string_view filename) {
    return config::text::iequals(extensionOf(filename), "na") ? EcholistFormat::Na
                                                              : EcholistFormat::Lst;
}

bool isEcholistName(std::string_view filename) {
    const std::string_view extension = extensionOf(filename);
    return config::text::iequals(extension, "lst") ||
           config::text::iequals(extension, "na");
}

ParseResult parseEcholist(std::string_view text, EcholistFormat format) {
    ParseResult result;

    const std::vector<std::string> lines = config::text::splitLines(text);
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string_view raw = lines[i];
        const size_t end = raw.find(kDosEof);
        const bool ended = end != std::string_view::npos;
        if (ended) raw = raw.substr(0, end);

        const std::string_view line = config::text::trim(raw);
        // `;` is the comment both formats are written with, and an echolist
        // carries a great deal of it: the header naming the region, the rules
        // for getting an echo onto the backbone, and the comoderators standing
        // above the echo they help moderate.
        if (!line.empty() && line.front() != ';') {
            if (format == EcholistFormat::Lst) {
                readLstLine(line, static_cast<int>(i) + 1, result);
            } else {
                readNaLine(line, result);
            }
        }
        if (ended) break;
    }

    return result;
}

}  // namespace amberedit::echolist
