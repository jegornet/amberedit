#include "app/msg_template.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string_view>
#include <utility>

#include "config/text_util.hpp"

namespace amberedit::app {
namespace {

/// The first word of a name, and the last — what @cfname and @clname are.
std::string firstWord(std::string_view name) {
    const size_t end = name.find(' ');
    return std::string(end == std::string_view::npos ? name : name.substr(0, end));
}

std::string lastWord(std::string_view name) {
    const size_t at = name.rfind(' ');
    return std::string(at == std::string_view::npos ? name : name.substr(at + 1));
}

/// Pads or cuts to the widths GoldED's @_name / @_addr tokens have always had.
std::string fixedWidth(std::string text, size_t width) {
    text.resize(width, ' ');
    return text;
}

/// Whether the text at `pos` begins with `token`, ignoring case, and is not
/// merely the start of a longer word of the template's own.
bool tokenAt(std::string_view text, size_t pos, std::string_view token) {
    if (pos + token.size() > text.size()) return false;
    return config::text::iequals(text.substr(pos, token.size()), token);
}

/// The conditional tokens, and whether each one holds for this message.
/// Everything AmberEdit never produces — a comment, the quote buffer — is
/// simply false, which drops those lines from a template written for a reader
/// that does.
bool conditionHolds(std::string_view name, const TemplateContext& context) {
    if (config::text::iequals(name, "new")) return context.isNew;
    if (config::text::iequals(name, "reply")) return context.isReply && !context.isQuoted;
    if (config::text::iequals(name, "quoted")) return context.isQuoted;
    if (config::text::iequals(name, "moved")) return context.isMoved;
    if (config::text::iequals(name, "forward")) return context.isForward;
    if (config::text::iequals(name, "changed")) return context.isChanged;
    if (config::text::iequals(name, "echo")) return context.isEcho;
    if (config::text::iequals(name, "net")) return context.isNet;
    if (config::text::iequals(name, "local")) return context.isLocal;
    if (config::text::iequals(name, "moderator")) {
        return config::text::toLower(context.tname).find("moderator") !=
               std::string::npos;
    }
    // comment, quotebuf: AmberEdit makes neither.
    return false;
}

const std::array<std::string_view, 12> kConditionals{
    "quotebuf", "moderator", "comment", "changed", "forward", "quoted",
    "reply",    "local",     "moved",   "echo",    "new",     "net"};

/// A line with its leading conditional tokens taken off: what is left to write,
/// which of them it carried, and whether it is to be written at all.
struct LinePrefix {
    std::string_view rest;
    /// The conditionals the line began with, in the order it wrote them. They
    /// stack: "@quoted@position" is both.
    std::vector<std::string_view> conditions;
    bool position{false};
    /// One of the conditionals does not hold, so the line is not in this
    /// message. What is left of `rest` then says nothing.
    bool dropped{false};
};

LinePrefix stripConditionals(std::string_view line, const TemplateContext& context) {
    LinePrefix out;
    for (bool matched = true; matched && !out.dropped;) {
        matched = false;
        if (line.size() < 2 || line[0] != '@') break;

        if (tokenAt(line, 1, "position")) {
            out.position = true;
            line.remove_prefix(1 + 8);
            matched = true;
            continue;
        }
        for (const auto& name : kConditionals) {
            if (!tokenAt(line, 1, name)) continue;
            if (!conditionHolds(name, context)) out.dropped = true;
            out.conditions.push_back(name);
            line.remove_prefix(1 + name.size());
            matched = true;
            break;
        }
    }
    out.rest = line;
    return out;
}

/// A token and what it stands for, in the order they are tried.
using Replacements = std::vector<std::pair<std::string, std::string>>;

/// The replacement tokens, longest first so that @cdate is not read as @c.
Replacements replacements(const TemplateContext& context) {
    const std::string cfname = firstWord(context.cname);
    const std::string clname = lastWord(context.cname);
    const std::string ofname = firstWord(context.oname);
    const std::string olname = lastWord(context.oname);
    const std::string tfname = firstWord(context.tname);
    const std::string tlname = lastWord(context.tname);
    const std::string dfname = firstWord(context.dname);
    const std::string dlname = lastWord(context.dname);

    std::vector<std::pair<std::string, std::string>> table{
        // Fixed-width forms first: they are the longer spelling of the same
        // tokens, and the longest match has to win.
        {"_cname", fixedWidth(context.cname, 34)},
        {"_oname", fixedWidth(context.oname, 34)},
        {"_tname", fixedWidth(context.tname, 34)},
        {"_dname", fixedWidth(context.dname, 34)},
        {"_caddr", fixedWidth(context.caddr, 19)},
        {"_oaddr", fixedWidth(context.oaddr, 19)},
        {"_taddr", fixedWidth(context.taddr, 19)},
        {"_daddr", fixedWidth(context.daddr, 19)},

        {"c3daddr", context.c3daddr},
        {"o3daddr", context.o3daddr},
        {"t3daddr", context.t3daddr},
        {"d3daddr", context.d3daddr},
        {"ctzoffset", context.ctzoffset},
        {"otzoffset", context.otzoffset},
        {"omessageid", context.omsgid},
        {"areaname", context.areaname},
        {"areapath", context.areapath},
        {"areatype", context.areatype},
        {"longpid", context.longpid},
        {"tearline", context.tearline},
        {"serialno", ""},
        {"subject", context.subject},
        {"tagline", context.tagline},
        {"version", context.version},
        {"cpseudo", cfname},
        {"opseudo", ofname},
        {"tpseudo", tfname},
        {"dpseudo", dfname},
        {"origin", context.origin},
        {"cfname", cfname},
        {"clname", clname},
        {"ofname", ofname},
        {"olname", olname},
        {"tfname", tfname},
        {"tlname", tlname},
        {"dfname", dfname},
        {"dlname", dlname},
        {"pseudo", tfname},
        {"omsgid", context.omsgid},
        {"cname", context.cname},
        {"caddr", context.caddr},
        {"cdate", context.cdate},
        {"ctime", context.ctime},
        {"cecho", context.cecho},
        {"cdesc", context.cdesc},
        {"oname", context.oname},
        {"oaddr", context.oaddr},
        {"odate", context.odate},
        {"otime", context.otime},
        {"oecho", context.oecho},
        {"odesc", context.odesc},
        {"ofrom", context.oname},
        {"tname", context.tname},
        {"taddr", context.taddr},
        {"dname", context.dname},
        {"daddr", context.daddr},
        {"oto", context.dname},
        {"pid", context.pid},
        {"ver", context.version},
        {"rev", context.version},
    };
    // Longest first, so that @cdate is never read as @c followed by "date"
    // and @_cname never as @_c. Sorted here rather than trusted to the order
    // above, which is written for reading.
    std::stable_sort(table.begin(), table.end(), [](const auto& a, const auto& b) {
        return a.first.size() > b.first.size();
    });
    return table;
}

/// Whose name a name token writes, or nothing when the token is not one.
///
/// Which of the {mine}{theirs} parameters applies is decided by whose name the
/// token stands for, not by what part of it the token writes: @ofname puts a
/// first name in the message, but it is the whole name that says whether it is
/// mine. @areaname is no name of a person at all, and takes no parameters.
const std::string* nameBehind(std::string_view token, const TemplateContext& context) {
    if (!token.empty() && token.front() == '_') token.remove_prefix(1);
    // Bare @pseudo is the recipient's, as @tpseudo is.
    if (config::text::iequals(token, "pseudo")) return &context.tname;
    if (token.size() < 2) return nullptr;

    const std::string_view part = token.substr(1);
    if (!config::text::iequals(part, "name") && !config::text::iequals(part, "fname") &&
        !config::text::iequals(part, "lname") && !config::text::iequals(part, "pseudo")) {
        return nullptr;
    }
    switch (static_cast<char>(std::tolower(static_cast<unsigned char>(token.front())))) {
        case 'c': return &context.cname;
        case 'o': return &context.oname;
        case 't': return &context.tname;
        case 'd': return &context.dname;
        default: return nullptr;
    }
}

/// Which of a name token's {mine}{theirs}{whoto} parameters applies, and how
/// far the parameters run.
///
/// The braces are the template's way of writing "I" where the name would be
/// mine and "you" where it would be my opponent's — the one I am writing to.
/// A third party is neither, and is named outright: answering a message Stas
/// wrote to Nil A gives "you wrote to Nil A", not "you wrote to you". `value`
/// is what the token itself writes, `full` the whole name it belongs to.
std::string applyNameParameters(std::string_view text, size_t& pos,
                                const std::string& value, const std::string& full,
                                const TemplateContext& context) {
    std::vector<std::string> params;
    while (pos < text.size() && text[pos] == '{') {
        const size_t end = text.find('}', pos);
        if (end == std::string_view::npos) break;
        params.emplace_back(text.substr(pos + 1, end - pos - 1));
        pos = end + 1;
    }
    if (params.empty() || full.empty()) return value;

    if (config::text::iequals(full, context.cname)) return params[0];
    // "All" and its like are whoever the area is addressed to, which is what
    // the third parameter is for.
    if (params.size() >= 3 &&
        (config::text::iequals(full, "All") || config::text::iequals(full, "Everyone"))) {
        return params[2];
    }
    // The opponent is whoever this message is being written to: the author of
    // the message being answered, in a reply.
    if (params.size() >= 2 && !context.tname.empty() &&
        config::text::iequals(full, context.tname)) {
        return params[1];
    }
    return value;
}

/// The @tokens of one line replaced by what they stand for. Everything else,
/// the token that stands for nothing included, is copied through as written.
std::string substitute(std::string_view line, const Replacements& table,
                       const TemplateContext& context) {
    std::string out;
    for (size_t pos = 0; pos < line.size();) {
        if (line[pos] != '@') {
            out += line[pos++];
            continue;
        }
        if (pos + 1 < line.size() && line[pos + 1] == '@') {
            out += '@';  // "@@" is how a template writes a literal '@'
            pos += 2;
            continue;
        }

        const auto match = std::find_if(
            table.begin(), table.end(),
            [&](const auto& entry) { return tokenAt(line, pos + 1, entry.first); });
        if (match == table.end()) {
            out += line[pos++];
            continue;
        }
        pos += 1 + match->first.size();

        const std::string* full = nameBehind(match->first, context);
        out += full != nullptr
                   ? applyNameParameters(line, pos, match->second, *full, context)
                   : match->second;
    }
    return out;
}

/// Everything after the token on an insert line is ignored, so the argument is
/// simply what follows a space.
std::string argumentOf(std::string_view line, size_t after) {
    while (after < line.size() && line[after] == ' ') ++after;
    return std::string(config::text::trim(line.substr(after)));
}

}  // namespace

TemplateResult expandTemplate(const std::string& text, const TemplateContext& context) {
    TemplateResult result;
    const auto table = replacements(context);

    for (const auto& raw : config::text::splitLines(text)) {
        // A semicolon in the first column comments the line out.
        if (!raw.empty() && raw[0] == ';') continue;

        // Leading conditionals decide whether the line appears at all.
        const LinePrefix prefix = stripConditionals(raw, context);
        if (prefix.dropped) continue;
        std::string_view line = prefix.rest;
        if (prefix.position) result.cursorLine = static_cast<int>(result.lines.size());

        // Insert tokens replace the whole line with what they name.
        if (!line.empty() && line[0] == '@') {
            if (tokenAt(line, 1, "quotebuf")) continue;  // no quote buffer here
            if (tokenAt(line, 1, "quote")) {
                result.lines.insert(result.lines.end(), context.quote.begin(),
                                    context.quote.end());
                continue;
            }
            // @message inserts the original in full: the message being
            // forwarded, unquoted. It is empty for anything else — a reply
            // quotes through @quote instead, and AmberEdit writes no changed
            // messages — and then the line goes, as an insert token's line does
            // either way.
            if (tokenAt(line, 1, "message")) {
                result.lines.insert(result.lines.end(), context.message.begin(),
                                    context.message.end());
                continue;
            }
            if (tokenAt(line, 1, "include")) {
                std::filesystem::path path{argumentOf(line, 1 + 7)};
                if (path.is_relative() && !context.includeDir.empty()) {
                    path = std::filesystem::path(context.includeDir) / path;
                }
                try {
                    for (auto& included : config::text::splitLines(
                             config::text::readFile(path.string()))) {
                        result.lines.push_back(std::move(included));
                    }
                } catch (const std::exception&) {
                    // A missing include is the template's problem, not the
                    // message's: say so where it would have gone.
                    result.lines.push_back("[template: cannot read " + path.string() +
                                           "]");
                }
                continue;
            }
            const auto header = [&](std::string_view token, std::string& into,
                                    bool& force, bool forced) {
                if (!tokenAt(line, 1, token)) return false;
                into = argumentOf(line, 1 + token.size());
                // The quotes GoldED writes round the value are punctuation.
                if (into.size() >= 2 && into.front() == '"' && into.back() == '"') {
                    into = into.substr(1, into.size() - 2);
                }
                force = forced;
                return true;
            };
            if (header("forcefrom", result.setFrom, result.forceFrom, true)) continue;
            if (header("forceto", result.setTo, result.forceTo, true)) continue;
            if (header("forcesubj", result.setSubject, result.forceSubject, true))
                continue;
            if (header("setfrom", result.setFrom, result.forceFrom, false)) continue;
            if (header("setto", result.setTo, result.forceTo, false)) continue;
            if (header("setsubj", result.setSubject, result.forceSubject, false))
                continue;

            // Tokens that mean nothing here: a cookie file, message attributes,
            // GoldED's own language and export settings. The line goes, since
            // an insert token takes the whole line with it either way.
            if (tokenAt(line, 1, "random") || tokenAt(line, 1, "attrib") ||
                tokenAt(line, 1, "loadlanguage") || tokenAt(line, 1, "xlatexport")) {
                continue;
            }
        }

        // What is left is substituted where it stands.
        result.lines.push_back(substitute(line, table, context));
    }
    return result;
}

std::vector<std::string> conditionalLines(const std::string& text,
                                          std::string_view condition,
                                          const TemplateContext& context) {
    std::vector<std::string> lines;
    const auto table = replacements(context);

    for (const auto& raw : config::text::splitLines(text)) {
        if (!raw.empty() && raw[0] == ';') continue;

        const LinePrefix prefix = stripConditionals(raw, context);
        if (prefix.dropped) continue;
        const bool asked = std::any_of(prefix.conditions.begin(), prefix.conditions.end(),
                                       [condition](std::string_view name) {
                                           return config::text::iequals(name, condition);
                                       });
        if (!asked) continue;
        lines.push_back(substitute(prefix.rest, table, context));
    }
    return lines;
}

std::string expandTokens(std::string_view line, const TemplateContext& context) {
    return substitute(line, replacements(context), context);
}

}  // namespace amberedit::app
