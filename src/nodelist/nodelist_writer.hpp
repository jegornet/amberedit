#pragma once

#include <cstddef>
#include <ctime>
#include <string>
#include <vector>

#include "support/result.hpp"
#include "nodelist/node_entry.hpp"
#include "nodelist/nodelist_source.hpp"

namespace amberedit::nodelist {

/// One nodelist that went into the compiled file: which file it was and what it
/// was when it was read, and the entries that came out of it.
///
/// The state is written into the file for two reasons at once — a node can say
/// which nodelist it came from, and the next start can tell whether that
/// nodelist is still the same file. A source with no entries is meaningful and
/// is written like any other: it is a nodelist that was not there, or would not
/// read, and recording what it was then is what stops every start from trying
/// to compile it again.
struct DbSource {
    SourceState state;
    std::vector<NodeEntry> entries;
};

struct WriteReport {
    size_t nodes{0};
    size_t points{0};
    /// Entries dropped for standing at an address an earlier one already holds.
    size_t duplicates{0};
    size_t bytes{0};
};

/// Writes the compiled nodelist, naming the file for anything that stops it.
///
/// Where two entries stand at one address the earlier source wins, and inside
/// one source the earlier line does: the config's order of `nodelist` lines is
/// the only statement of precedence anybody has made, and it reads as one.
///
/// The file is written beside its destination and renamed over it, so a reader
/// opening the old one while this runs either goes on reading the whole of the
/// old file or opens the whole of the new one. There is no moment at which the
/// path names half a nodelist.
[[nodiscard]] Result<WriteReport> writeNodelistDb(const std::string& path,
                                                  const std::vector<DbSource>& sources,
                                                  std::time_t builtAt);

}  // namespace amberedit::nodelist
