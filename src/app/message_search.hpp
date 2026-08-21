#pragma once

#include <string>
#include <vector>

#include "domain/message.hpp"
#include "encoding/text_search.hpp"

/// Finding a message in the area being read: what the reader's `f` asks for.
///
/// The matching itself is `encoding::TextSearch` — case folded by the charset
/// the message declares rather than by the locale. What is here is which parts
/// of a message a search looks at.
namespace amberedit::app {

/// How much of a message a search reads.
///
/// The two are what the Find dialog asks about, and the header is in both: a
/// search of the text alone would answer "no" for the message whose subject is
/// the very words typed, and nobody looking for a word means "except where it
/// is written at the top".
enum class SearchScope {
    HeaderAndText,  ///< the header fields and the message's own text
    Header,         ///< the header fields alone
};

/// The header fields a search reads, in the order the reader shows them: the
/// From name and the address under it, the To name and its address, and the
/// subject. Named as a list rather than searched one by one so that what a
/// search looks at is written down once.
///
/// The addresses go in as they are written — `2:5020/1042.7` — which is how
/// somebody looking for a node writes one. An address the message does not carry
/// is left out rather than added as an empty string: in echomail the
/// destination addresses nobody, and the reader shows no such field either.
[[nodiscard]] std::vector<std::string> searchableHeader(
    const domain::MessageHeader& header);

/// Whether any of those fields holds what is being looked for.
[[nodiscard]] bool matchesHeader(const encoding::TextSearch& search,
                                 const domain::MessageHeader& header);

/// Whether the message's text does — the lines the reader shows, service data
/// left out. A MSGID is not something anybody searches for, and the kludges are
/// off the screen unless `k` has been pressed.
[[nodiscard]] bool matchesBody(const encoding::TextSearch& search,
                               const domain::MessageBody& body);

}  // namespace amberedit::app
