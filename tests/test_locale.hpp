#pragma once

#include <array>
#include <clocale>
#include <cstdlib>
#include <optional>
#include <string>

#include "sys/env.hpp"

namespace amberedit::test {

/// The locale the process is in, put back when this goes out of scope.
///
/// A test that asks for another language changes the whole process: the
/// environment is where gettext reads the language from, and `setlocale` has
/// one answer per program, not one per test. Left as the test found it, the
/// next test to measure a character is measured under it — `wcwidth()` reports
/// one column for a CJK glyph under `C`, and a row of Japanese comes out half
/// as wide as it will be drawn.
///
/// Both halves have to be put back, and restoring the environment alone is not
/// enough: a `setlocale(LC_CTYPE, "")` made while the environment said `C`
/// leaves the process in `C` afterwards, whatever the variables say by then.
/// So the locale itself is saved as `setlocale` hands it over — POSIX has that
/// string ready to be handed straight back — and set again from the string
/// rather than re-read from an environment that may since have moved.
///
/// `LC_CTYPE` is guarded with the four names gettext reads, because it is the
/// category that says how wide a character is and the one a test is least
/// likely to think of.
class WithLocaleEnv {
public:
    WithLocaleEnv() {
        for (size_t i = 0; i < kNames.size(); ++i) {
            if (const char* was = ::getenv(kNames[i])) previous_[i] = std::string(was);
        }
        if (const char* was = std::setlocale(LC_ALL, nullptr)) locale_ = std::string(was);
    }
    ~WithLocaleEnv() {
        for (size_t i = 0; i < kNames.size(); ++i) {
            if (previous_[i]) {
                amberedit::sys::setEnvironment(kNames[i], previous_[i]->c_str());
            } else {
                amberedit::sys::unsetEnvironment(kNames[i]);
            }
        }
        if (locale_) std::setlocale(LC_ALL, locale_->c_str());
    }

    WithLocaleEnv(const WithLocaleEnv&) = delete;
    WithLocaleEnv& operator=(const WithLocaleEnv&) = delete;

private:
    static constexpr std::array<const char*, 5> kNames{"LANGUAGE", "LC_ALL",
                                                       "LC_MESSAGES", "LANG", "LC_CTYPE"};
    std::array<std::optional<std::string>, 5> previous_;
    std::optional<std::string> locale_;
};

}  // namespace amberedit::test
