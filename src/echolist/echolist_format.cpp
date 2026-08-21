#include "echolist/echolist_format.hpp"

#include "config/text_util.hpp"

namespace amberedit::echolist {

std::string foldTag(std::string_view tag) {
    const std::string_view trimmed = config::text::trim(tag);
    std::string out;
    out.reserve(trimmed.size());
    for (char c : trimmed) out += config::text::asciiLower(c);
    return out;
}

}  // namespace amberedit::echolist
