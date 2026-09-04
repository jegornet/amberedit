#include "sys/charset.hpp"

#include <clocale>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <langinfo.h>
#endif

namespace amberedit::sys {

std::string localeCodeset() {
#ifdef _WIN32
    return "CP" + std::to_string(::GetACP());
#else
    std::setlocale(LC_CTYPE, "");
    const char* name = nl_langinfo(CODESET);
    return name != nullptr ? std::string(name) : std::string();
#endif
}

}  // namespace amberedit::sys
