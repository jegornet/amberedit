#include "sys/env.hpp"

#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace amberedit::sys {

void setEnvironment(std::string_view name, std::string_view value) {
    // Both calls want C strings and neither keeps the pointers, so the copies
    // live no longer than the call.
    const std::string key(name);
    const std::string text(value);
#ifdef _WIN32
    ::_putenv_s(key.c_str(), text.c_str());
#else
    ::setenv(key.c_str(), text.c_str(), 1);
#endif
}

void unsetEnvironment(std::string_view name) {
    const std::string key(name);
#ifdef _WIN32
    ::_putenv_s(key.c_str(), "");
#else
    ::unsetenv(key.c_str());
#endif
}

std::string uiLanguages() {
#ifdef _WIN32
    // Asked twice, as this pair of calls always is: once for the room the answer
    // needs, once for the answer. What comes back is names one after another,
    // each ending in a null, and a second null ending the list.
    ULONG count = 0;
    ULONG length = 0;
    if (::GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &length) == 0) {
        return {};
    }

    std::wstring buffer(length, L'\0');
    if (::GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &length) ==
        0) {
        return {};
    }

    std::string list;
    for (const wchar_t* name = buffer.c_str(); *name != L'\0';) {
        std::string tag;
        for (; *name != L'\0'; ++name) {
            if (*name > 127) {  // not a language tag; take none of it
                tag.clear();
                break;
            }
            // `ru-RU` is gettext's `ru_RU`, and that is the whole difference.
            tag += *name == L'-' ? '_' : static_cast<char>(*name);
        }
        while (*name != L'\0') ++name;  // whatever was left of a rejected name
        ++name;                          // past the null that ended it
        if (tag.empty()) continue;
        if (!list.empty()) list += ':';
        list += tag;
    }
    return list;
#else
    return {};
#endif
}

std::string userTag() {
#ifdef _WIN32
    return {};
#else
    return std::to_string(static_cast<unsigned long>(::getuid()));
#endif
}

}  // namespace amberedit::sys
