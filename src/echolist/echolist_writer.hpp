#pragma once

#include <cstddef>
#include <ctime>
#include <string>
#include <vector>

#include "echolist/echolist_parser.hpp"
#include "echolist/echolist_source.hpp"

namespace amberedit::echolist {

/// One `echolist` line that went into the compiled file: which file it was and
/// what it was when it was read, and every entry that came out of it — out of
/// all its lists at once, where the line named an archive holding several.
///
/// The state is written into the file so that the next start can tell whether
/// that echolist is still the same file. A source with no entries is meaningful
/// and is written like any other: it is an echolist that was not there, or
/// would not read, and recording what it was then is what stops every start
/// from trying to compile it again.
struct DbSource {
    SourceState state;
    std::vector<EchoEntry> entries;
};

struct WriteReport {
    size_t areas{0};
    /// Entries dropped for naming a tag an earlier one already holds.
    size_t duplicates{0};
    size_t bytes{0};
};

/// Writes the compiled echolist. Throws std::runtime_error naming the file for
/// anything that stops it.
///
/// Where two entries name one tag the earlier source wins, and inside one source
/// the earlier entry does: the config's order of `echolist` lines is the only
/// statement of precedence anybody has made, and it reads as one.
///
/// The file is written beside its destination and renamed over it, so a reader
/// opening the old one while this runs either goes on reading the whole of the
/// old file or opens the whole of the new one. There is no moment at which the
/// path names half an echolist.
WriteReport writeEcholistDb(const std::string& path, const std::vector<DbSource>& sources,
                            std::time_t builtAt);

}  // namespace amberedit::echolist
