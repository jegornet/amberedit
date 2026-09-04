#include "config/path_map.hpp"

#include <cstddef>
#include <optional>
#include <utility>

#include "config/text_util.hpp"

namespace amberedit::config {

namespace {

bool isSeparator(char c) {
    return c == '/' || c == '\\';
}

/// The components of a path, in order, with the separators and the empty
/// stretches between two of them dropped: `c:\fido\\msgbase\` is
/// {"c:", "fido", "msgbase"}.
///
/// Both separators split, because a config written for DOS may hold either and
/// a path that came through a variable may hold both.
std::vector<std::string_view> components(std::string_view path) {
    std::vector<std::string_view> parts;
    size_t i = 0;
    while (i < path.size()) {
        while (i < path.size() && isSeparator(path[i])) ++i;
        const size_t start = i;
        while (i < path.size() && !isSeparator(path[i])) ++i;
        if (i > start) parts.push_back(path.substr(start, i - start));
    }
    return parts;
}

/// Whether the path starts at a root. A drive letter is a component like any
/// other, so `c:\fido` is not rooted by this and `\fido` is — which is all the
/// question is for: a rule written `\fido` should not catch the relative
/// `fido\msgbase` that happens to spell the same words.
bool isRooted(std::string_view path) {
    return !path.empty() && isSeparator(path.front());
}

/// The separator the target is written with: a backslash only where it is the
/// one the target itself uses, so a config mapping one DOS tree onto another
/// keeps the spelling and everything else gets this machine's.
char separatorOf(std::string_view target) {
    if (target.find('\\') != std::string_view::npos &&
        target.find('/') == std::string_view::npos) {
        return '\\';
    }
    return '/';
}

/// How many components of `path` the rule's source covers, or nothing where it
/// covers none of it. Zero is a real answer: a source of nothing but separators
/// maps the whole tree it is rooted in.
std::optional<size_t> matchLength(const std::vector<std::string_view>& source,
                                  const std::vector<std::string_view>& path) {
    if (source.size() > path.size()) return std::nullopt;
    for (size_t i = 0; i < source.size(); ++i) {
        if (!text::iequals(source[i], path[i])) return std::nullopt;
    }
    return source.size();
}

}  // namespace

void PathMap::add(std::string source, std::string target) {
    rules_.push_back(PathMapping{std::move(source), std::move(target)});
}

bool PathMap::maps(std::string_view source) const {
    const auto wanted = components(source);
    const bool rooted = isRooted(source);
    for (const auto& rule : rules_) {
        if (isRooted(rule.source) != rooted) continue;
        const auto have = components(rule.source);
        if (have.size() != wanted.size()) continue;
        const auto matched = matchLength(have, wanted);
        if (matched) return true;
    }
    return false;
}

std::string PathMap::apply(const std::string& path) const {
    // An area with no base of its own has no path, and a config with no rules
    // is every config that reads a tosser running on this machine.
    if (rules_.empty() || path.empty()) return path;

    const auto parts = components(path);
    const bool rooted = isRooted(path);

    const PathMapping* best = nullptr;
    size_t bestLength = 0;
    for (const auto& rule : rules_) {
        if (isRooted(rule.source) != rooted) continue;
        const auto matched = matchLength(components(rule.source), parts);
        if (!matched) continue;
        // Strictly longer, so that two rules naming the same source leave the
        // first one standing — the config refuses that pair before it gets
        // here, and this is what the answer is if one ever reaches it.
        if (!best || *matched > bestLength) {
            best = &rule;
            bestLength = *matched;
        }
    }
    if (!best) return path;

    const char separator = separatorOf(best->target);
    std::string_view target = best->target;
    while (target.size() > 1 && isSeparator(target.back())) target.remove_suffix(1);
    if (target.size() == 1 && isSeparator(target.front())) target = {};

    std::string mapped(target);
    for (size_t i = bestLength; i < parts.size(); ++i) {
        mapped += separator;
        mapped += parts[i];
    }
    // The target was a bare root and the path was the whole of the source, so
    // there is nothing left to have put a separator in front of.
    if (mapped.empty()) mapped = std::string(1, separator);
    return mapped;
}

}  // namespace amberedit::config
