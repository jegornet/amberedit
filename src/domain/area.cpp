#include "domain/area.hpp"

#include <string>

namespace amberedit::domain {
namespace {

/// ASCII case folding, spelled out here rather than borrowed from
/// `config/text_util`: the domain includes nothing from the layers above it,
/// and the words compared are ASCII whatever the locale is.
bool sameWord(std::string_view word, std::string_view name) {
    if (word.size() != name.size()) return false;
    for (size_t i = 0; i < word.size(); ++i) {
        char c = word[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != name[i]) return false;
    }
    return true;
}

}  // namespace

std::string toString(MsgBaseType type) {
    switch (type) {
        case MsgBaseType::Squish: return "squish";
        case MsgBaseType::Jam: return "jam";
        case MsgBaseType::Sdm: return "msg";
        case MsgBaseType::Passthrough: return "passthrough";
        case MsgBaseType::Unknown: break;
    }
    return "unknown";
}

std::optional<MsgBaseType> parseMsgBaseType(std::string_view word) {
    if (sameWord(word, "squish")) return MsgBaseType::Squish;
    if (sameWord(word, "jam")) return MsgBaseType::Jam;
    // Three spellings of one thing: `msg` is what toString writes, and a tosser
    // config is as likely to have been written with either of the other two.
    if (sameWord(word, "msg") || sameWord(word, "sdm") || sameWord(word, "fido"))
        return MsgBaseType::Sdm;
    if (sameWord(word, "passthrough")) return MsgBaseType::Passthrough;
    return std::nullopt;
}

std::string toString(AreaKind kind) {
    switch (kind) {
        case AreaKind::Echo: return "echo";
        case AreaKind::Netmail: return "netmail";
        case AreaKind::Local: return "local";
        case AreaKind::Bad: return "bad";
        case AreaKind::Dupe: return "dupe";
    }
    return "echo";
}

std::optional<AreaKind> parseAreaKind(std::string_view word) {
    if (sameWord(word, "echo")) return AreaKind::Echo;
    if (sameWord(word, "netmail")) return AreaKind::Netmail;
    if (sameWord(word, "local")) return AreaKind::Local;
    if (sameWord(word, "bad")) return AreaKind::Bad;
    if (sameWord(word, "dupe")) return AreaKind::Dupe;
    return std::nullopt;
}

}  // namespace amberedit::domain
