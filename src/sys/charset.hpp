#pragma once

#include <string>

namespace amberedit::sys {

/// What the system says text is written in.

/// The codeset of the environment's own locale, which is what a file on disk is
/// assumed to be in when nothing else says otherwise. Installs that locale for
/// `LC_CTYPE` on the way, since a program starts in the C one and would
/// otherwise be told about that instead.
///
/// POSIX answers through `nl_langinfo(CODESET)`. Windows has no such call: the
/// equivalent is the ANSI code page, reported here in the `CP1251` spelling
/// that `charset_detector` and iconv already understand.
[[nodiscard]] std::string localeCodeset();

}  // namespace amberedit::sys
