#include "msgbase/echotoss_log.hpp"

#include <fstream>
#include <ios>
#include <string>

namespace amberedit::msgbase {

void appendEchotossLog(const std::string& path, const std::string& tag) {
    if (path.empty() || tag.empty()) return;
    // Binary, so that a line ends with the one LF (0x0A) the tossers reading
    // this file take: the Windows build would otherwise write CRLF for a '\n'.
    //
    // Opened and closed around the one line rather than held open, as the error
    // log is: a tosser may take the file away between two messages, and the
    // next message written then makes it again.
    std::ofstream out(path, std::ios::app | std::ios::binary);
    if (!out) return;
    out << tag << '\n';
}

}  // namespace amberedit::msgbase
