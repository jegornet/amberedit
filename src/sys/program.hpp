#pragma once

#include <filesystem>

namespace amberedit::sys {

/// Where this program's own binary is, asked of the system rather than worked
/// out from `argv[0]`.
///
/// `argv[0]` is whatever the caller chose to say — a bare name found on the
/// path, a relative path from a directory since changed — and is not a file
/// anything can be looked for beside. Every system will say where the running
/// image came from; this asks, and answers with an empty path where it will not.
///
/// What it is for: finding the data a program was installed alongside when the
/// path compiled into it is not where it ended up. That is the ordinary case on
/// Windows, which has no fixed prefix and where an archive is unpacked wherever
/// the user likes.
[[nodiscard]] std::filesystem::path executablePath();

}  // namespace amberedit::sys
