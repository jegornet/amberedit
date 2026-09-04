#pragma once

#include <string>
#include <string_view>

namespace amberedit::sys {

/// The two things AmberEdit asks of the environment that `<cstdlib>` will not
/// answer for. Reading a variable is `std::getenv` everywhere and is not here.

/// Sets a variable for this process and the children it starts, replacing any
/// value already there. POSIX spells this `setenv` and Windows `_putenv_s`.
///
/// Used for one thing only: putting `LANGUAGE` down so that libintl looks the
/// interface up again in another language.
void setEnvironment(std::string_view name, std::string_view value);

/// Takes a variable away again, as though it had never been set. POSIX spells
/// this `unsetenv`; Windows has no such call and setting a variable to nothing
/// is how a variable is removed there.
void unsetEnvironment(std::string_view name);

/// The languages the user wants their interface in, most wanted first, spelled
/// the way gettext spells them and joined the way `LANGUAGE` joins them:
/// `ru_RU:en_US`. Empty where the system does not say.
///
/// For Windows alone, and empty everywhere else. POSIX carries the answer in the
/// environment, where `setlocale` finds it and gettext reads it after; Windows
/// has no `LANG` and no `LC_MESSAGES` category in its C runtime, so nothing
/// reaches gettext at all and the interface stays English however the machine is
/// set up.
///
/// Asked of the display languages and not of the locale: a user may well have
/// Windows in one language and their dates and decimal points in another, and it
/// is the first of those that says what language to draw an interface in. The
/// list is what Windows keeps and what `LANGUAGE` wants, so it is handed over
/// whole rather than cut down to its first entry.
[[nodiscard]] std::string uiLanguages();

/// A tag that distinguishes this user's scratch directory from another's, or an
/// empty string where the system has already done that.
///
/// On POSIX the temporary directory is shared — `/tmp` belongs to everybody —
/// so the user id goes into the name and the directory is made private. Windows
/// hands every account a temporary directory of its own under its profile, so
/// there is nobody to be told apart from and the name stands unqualified.
[[nodiscard]] std::string userTag();

}  // namespace amberedit::sys
