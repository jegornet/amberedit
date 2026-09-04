#include "i18n/i18n.hpp"

#include <libintl.h>

#include <clocale>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "sys/env.hpp"
#include "sys/program.hpp"

#ifdef AMBEREDIT_HAVE_NL_MSG_CAT_CNTR
extern "C" {
/// How a program that changes `LANGUAGE` while it runs tells gettext to look
/// again. glibc and GNU libintl both export it and gettext's manual is where it
/// is documented, but it is in no header, so it is declared here and the build
/// probes for it — `CMakeLists.txt`, the libintl block.
extern int _nl_msg_cat_cntr;  // NOLINT(bugprone-reserved-identifier)
}
#endif

namespace amberedit::i18n {
namespace {

/// The domain the program's own messages are under, and what the catalog is
/// called on disk. Settled from the path rather than written here, so that the
/// two cannot disagree.
std::string domain;

/// Whether the catalog `load()` was given is really answering.
bool active = false;

/// Tells gettext that `LANGUAGE` has changed under it. Without it the first
/// answer is cached for the life of the process, and a test that loaded a
/// catalog and put it down again would go on being answered in Russian.
void invalidateCache() {
#ifdef AMBEREDIT_HAVE_NL_MSG_CAT_CNTR
    ++_nl_msg_cat_cntr;
#endif
}

/// Whether a catalog is loaded, asked of gettext itself.
///
/// The entry with the empty msgid is a catalog's header — the charset, the
/// plural rule, the translator — and gettext answers with it when the catalog is
/// open and with the empty string when it is not. It is the only way to find out:
/// nothing in the API reports whether `bindtextdomain()` found anything, and a
/// message that came back untranslated could as easily be one the catalog does
/// not carry.
bool catalogAnswers() {
    return gettext("")[0] != '\0';
}

/// The directory names a language could have its catalog under, most particular
/// first: `ru_RU.UTF-8` is also `ru_RU` and also `ru`, which is how gettext
/// itself widens a search.
void widen(std::string value, std::vector<std::string>& into) {
    // The charset and the modifier are not part of a directory name.
    value = value.substr(0, value.find('@'));
    value = value.substr(0, value.find('.'));
    if (value.empty() || value == "C" || value == "POSIX") return;
    into.push_back(value);
    const size_t underscore = value.find('_');
    if (underscore != std::string::npos) into.push_back(value.substr(0, underscore));
}

/// Every language the environment asks for, in gettext's own order of
/// preference, or empty where it asks for none.
///
/// `LANGUAGE` first and as a colon-separated list, because that is what gettext
/// reads it as; then the three locale variables, of which only the first that is
/// set counts. `C` and `POSIX` are not a language — they are the absence of one,
/// and the answer to them is the English the program is written in.
std::vector<std::string> languagesAsked() {
    std::vector<std::string> asked;

    if (const char* list = std::getenv("LANGUAGE"); list != nullptr) {
        std::string rest(list);
        while (!rest.empty()) {
            const size_t colon = rest.find(':');
            widen(rest.substr(0, colon), asked);
            if (colon == std::string::npos) break;
            rest = rest.substr(colon + 1);
        }
    }

    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(name);
        if (value == nullptr || value[0] == '\0') continue;
        widen(value, asked);
        break;
    }
    return asked;
}

/// Where this build put its catalogs, or where it will install them.
///
/// Three places, in this order:
///
///   * the build's own. A binary that has only been built is already translated,
///     which is the whole point of compiling both paths in. On any machine the
///     binary was shipped to, that directory is not there.
///   * beside the binary, at `../share/locale`. `AMBEREDIT_LOCALEDIR` is an
///     absolute path fixed when the build ran, which is right for a package
///     installed into the prefix it was built for and wrong for anything moved
///     since. Windows is the case where it is always wrong: there is no fixed
///     prefix there, and the zip is unpacked wherever the user likes — so the
///     catalogs shipped in it were never found and the interface stayed English
///     however `LANGUAGE` was set. On a package installed as intended this names
///     the same directory as the line below, so it changes nothing there.
///   * the path the build was told to install to, which is what a package uses.
std::string catalogDirectory() {
    std::error_code ec;
    if (std::filesystem::is_directory(AMBEREDIT_BUILD_LOCALEDIR, ec)) {
        return AMBEREDIT_BUILD_LOCALEDIR;
    }

    if (const std::filesystem::path program = sys::executablePath(); !program.empty()) {
        const std::filesystem::path beside =
            program.parent_path() / ".." / "share" / "locale";
        if (std::filesystem::is_directory(beside, ec)) {
            return std::filesystem::weakly_canonical(beside, ec).string();
        }
    }

    return AMBEREDIT_LOCALEDIR;
}

/// Whether a catalog for one of those languages is really on disk.
///
/// What tells a language AmberEdit has no translation for — nothing to complain
/// about — from one it has and could not use, which is worth a line.
bool catalogExistsFor(const std::vector<std::string>& languages,
                      const std::string& directory) {
    std::error_code ec;
    for (const std::string& language : languages) {
        const std::filesystem::path candidate =
            std::filesystem::path(directory) / language / "LC_MESSAGES" / "amberedit.mo";
        if (std::filesystem::is_regular_file(candidate, ec)) return true;
    }
    return false;
}

}  // namespace

/// Puts the system's languages into the environment where the user has named
/// none, so that gettext has something to go on.
///
/// Only where they have named none: `LANGUAGE`, `LC_ALL`, `LC_MESSAGES` and
/// `LANG` are all the user's own way of saying what they want, and a program
/// that overrode one of them would be answering a question nobody asked. That
/// this runs at all therefore means every one of them was unset.
///
/// It does nothing on POSIX, where the environment already carries the answer.
/// On Windows it carries nothing — there is no `LANG` there and the C runtime
/// has no `LC_MESSAGES` category — so a machine set to Russian would draw an
/// English interface until its owner worked out which variable to set by hand.
///
/// Both variables, and this is the part that is not obvious: `LANGUAGE` alone is
/// not enough, because gettext ignores it while the locale is `C`, and that is
/// what a Windows machine with the UTF-8 option turned on reports. Nor does
/// `setlocale(LC_MESSAGES, "ru_RU")` help — it takes the name and answers with
/// it, and gettext goes on drawing English, because on Windows libintl reads the
/// language out of the environment rather than out of the runtime's locale.
/// `LC_ALL` is what it reads and so what is set; it is the broadest of the four,
/// and safe here for the reason above — there was nothing of the user's to
/// override.
void adoptSystemLanguage() {
    for (const char* named : {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(named);
        if (value != nullptr && value[0] != '\0') return;
    }

    const std::string languages = sys::uiLanguages();
    if (languages.empty()) return;

    sys::setEnvironment("LANGUAGE", languages);
    // The first of them: `LC_ALL` names one locale, where `LANGUAGE` is a list
    // of them in the order they are wanted.
    sys::setEnvironment("LC_ALL", languages.substr(0, languages.find(':')));
}

Started start() {
    domain = "amberedit";
    adoptSystemLanguage();
    const std::string directory = catalogDirectory();
    ::bindtextdomain(domain.c_str(), directory.c_str());
    // Whatever the catalog was compiled in, it reaches this program as UTF-8:
    // everything above `ui/term` is UTF-8 and the terminal layer encodes on the
    // way out.
    ::bind_textdomain_codeset(domain.c_str(), "UTF-8");
    ::textdomain(domain.c_str());

    // Only this category. `LC_CTYPE` decides what the terminal can draw and is
    // `term::ensureUtf8Locale()`'s to settle, for reasons that have nothing to do
    // with which language the words are in.
    const char* got = std::setlocale(LC_MESSAGES, "");
    invalidateCache();

    Started started;
    if (got != nullptr) started.locale = got;
    active = catalogAnswers();
    if (active) return started;

    // English is very often the right answer and carries no complaint: nothing
    // was asked for, or what was asked for is a language there is no translation
    // into. The one case worth a line is a language whose catalog is right there
    // and which gettext would not use — which is the locale's doing, and is what
    // a stock Debian or Ubuntu does, having generated none at all.
    const std::vector<std::string> asked = languagesAsked();
    if (asked.empty() || !catalogExistsFor(asked, directory)) return started;

    started.warning = format(
        "the interface is in English: {0} is translated, but this system "
        "has no locale for it and gettext will not translate under `{1}`. "
        "Generate one — `apt install locales && locale-gen ru_RU.UTF-8` on "
        "Debian and Ubuntu, `dnf install glibc-langpack-{0}` on RHEL and "
        "Fedora — and `locale -a` lists what a system already has.",
        {asked.back(), started.locale.empty() ? "C" : started.locale});
    return started;
}

void clear() {
    // There is no unbinding in libintl, so the catalogs are left where they are
    // and the language is pointed at one that has none. `en` is the interface's
    // own language: a catalog for it would be a translation of English into
    // English and nobody writes one.
    sys::setEnvironment("LANGUAGE", "en");
    invalidateCache();
    active = false;
}

bool translating() {
    return active;
}

const char* translate(const char* msgid) {
    return ::gettext(msgid);
}

const char* translate(const char* context, const char* msgid) {
    // gettext stores a context and its msgid as one key with a US separator
    // between them, which is what `pgettext` is underneath and why a context
    // costs no second catalog.
    std::string key(context);
    key += '\x04';
    key += msgid;

    const char* found = domain.empty()
                            ? ::gettext(key.c_str())
                            : ::dcgettext(domain.c_str(), key.c_str(), LC_MESSAGES);
    // Nothing was found where what came back is the key that was asked with —
    // gettext hands the msgid straight back — and then it is the bare message
    // that is wanted, without the context in front of it.
    if (found == key.c_str()) return msgid;
    return found;
}

const char* plural(const char* one, const char* many, unsigned long n) {
    return ::ngettext(one, many, n);
}

std::string format(std::string_view pattern,
                   std::initializer_list<std::string_view> arguments) {
    std::string out;
    out.reserve(pattern.size());

    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] != '{') {
            out += pattern[i];
            continue;
        }

        // The digits between the braces, and nothing else: `{}` and `{x}` are
        // not placeholders and stand as they were written. A translation is text
        // somebody else wrote, and the one thing it must not be able to do is
        // ask for an argument that is not there.
        size_t at = i + 1;
        size_t index = 0;
        bool digits = false;
        while (at < pattern.size() && pattern[at] >= '0' && pattern[at] <= '9') {
            index = (index * 10) + static_cast<size_t>(pattern[at] - '0');
            digits = true;
            ++at;
        }
        if (!digits || at >= pattern.size() || pattern[at] != '}' ||
            index >= arguments.size()) {
            out += pattern[i];
            continue;
        }

        out += *(arguments.begin() + static_cast<long>(index));
        i = at;
    }
    return out;
}

}  // namespace amberedit::i18n
