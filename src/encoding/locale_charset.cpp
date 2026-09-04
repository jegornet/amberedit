#include "encoding/locale_charset.hpp"

#include <string>

#include "sys/charset.hpp"

namespace amberedit::encoding {

const std::string& localeCharset() {
    // Function-local so that the answer is settled on the first call, whether
    // that comes from the compile at startup or from a test, and so that
    // nothing here has to be sequenced against anything else that reads the
    // locale.
    //
    // `sys::localeCodeset()` installs the environment's locale on the way, which
    // is what makes the answer the user's rather than the C one every program
    // starts in. `term::ensureUtf8Locale()` does the same thing first and may
    // then install a UTF-8 locale over a C one; that runs later and is about
    // what the terminal is written in, not about what a file on disk was.
    static const std::string codeset = sys::localeCodeset();
    return codeset;
}

}  // namespace amberedit::encoding
