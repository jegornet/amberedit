#pragma once

#include <cstdint>
#include <string_view>

namespace amberedit::msgbase {

/// The CRC-32 JAM keys its records by: the reflected polynomial edb88320H
/// seeded with ffffffffH and *not* inverted at the end, over the text in lower
/// case. Only A-Z are lowered — the JAM specification requires exactly that and
/// no more, so a Cyrillic name hashes as written and every implementation
/// agrees on the result.
///
/// Three things in a JAM base are this hash: the lastread record's key, the
/// index record's copy of the recipient's name, and the MSGID/REPLY CRCs a
/// header carries so that a thread can be relinked without reading the text.
[[nodiscard]] uint32_t jamCrc32(std::string_view text);

}  // namespace amberedit::msgbase
