#include "app/message_search.hpp"

#include <string>
#include <vector>

namespace amberedit::app {

std::vector<std::string> searchableHeader(const domain::MessageHeader& header) {
    std::vector<std::string> fields{header.from};
    if (header.origAddr.isValid()) fields.push_back(header.origAddr.toString());
    fields.push_back(header.to);
    if (header.destAddr.isValid()) fields.push_back(header.destAddr.toString());
    fields.push_back(header.subject);
    return fields;
}

bool matchesHeader(const encoding::TextSearch& search,
                   const domain::MessageHeader& header) {
    for (const std::string& field : searchableHeader(header)) {
        if (search.contains(field)) return true;
    }
    return false;
}

bool matchesBody(const encoding::TextSearch& search, const domain::MessageBody& body) {
    for (const auto& line : body.lines) {
        if (line.kludge) continue;
        if (search.contains(line.text)) return true;
    }
    return false;
}

}  // namespace amberedit::app
