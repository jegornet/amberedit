#pragma once

#include <string>

namespace amberedit::encoding {

/// The character set the locale names, as a name iconv takes.
///
/// This is what a file with no charset of its own and nothing declaring one is
/// read in: an `echolist` line that states none says "the same eight-bit world
/// this terminal lives in", and the locale is the only place that is written
/// down. Nothing about a message goes through here — a message declares its
/// charset in a CHRS kludge and falls back on `default_charset`, both of which
/// `CharsetDetector` answers.
///
/// `LC_CTYPE` is taken from the environment on the first call, exactly as
/// `term::ensureUtf8Locale()` does later, so the answer is the user's own
/// choice wherever they have made one. Where they have made none the C locale
/// answers `ANSI_X3.4-1968` and that is what comes back: it is the truth about
/// the machine, and a guess put in its place would be a silent mojibake in
/// whichever direction it guessed wrong. State the charset on the line instead.
///
/// Settled once and remembered, since it cannot change under a running process.
[[nodiscard]] const std::string& localeCharset();

}  // namespace amberedit::encoding
