#include "encoding/locale_charset.hpp"

#include <langinfo.h>

#include <clocale>

namespace amberedit::encoding {

const std::string& localeCharset() {
    // Function-local so that the answer is settled on the first call, whether
    // that comes from the compile at startup or from a test, and so that
    // nothing here has to be sequenced against anything else that reads the
    // locale.
    static const std::string codeset = [] {
        // Setting it from the environment is what makes nl_langinfo answer for
        // the user's locale rather than for the C one every program starts in.
        // `term::ensureUtf8Locale()` does the same thing first and may then
        // install a UTF-8 locale over a C one; that runs later and is about
        // what ncurses writes, not about what a file on disk was written in.
        std::setlocale(LC_CTYPE, "");
        const char* name = nl_langinfo(CODESET);
        return std::string(name != nullptr ? name : "");
    }();
    return codeset;
}

}  // namespace amberedit::encoding
