#include "i18n/i18n.hpp"

#include <libintl.h>

#include <clocale>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

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
/// The build's own first: a binary that has only been built is already
/// translated, which is the whole point of compiling both in. On any machine the
/// binary was shipped to that directory does not exist, and the installed one
/// answers.
const char* catalogDirectory() {
    std::error_code ec;
    if (std::filesystem::is_directory(AMBEREDIT_BUILD_LOCALEDIR, ec)) {
        return AMBEREDIT_BUILD_LOCALEDIR;
    }
    return AMBEREDIT_LOCALEDIR;
}

/// Whether a catalog for one of those languages is really on disk.
///
/// What tells a language AmberEdit has no translation for — nothing to complain
/// about — from one it has and could not use, which is worth a line.
bool catalogExistsFor(const std::vector<std::string>& languages, const char* directory) {
    std::error_code ec;
    for (const std::string& language : languages) {
        const std::filesystem::path candidate =
            std::filesystem::path(directory) / language / "LC_MESSAGES" / "amberedit.mo";
        if (std::filesystem::is_regular_file(candidate, ec)) return true;
    }
    return false;
}

}  // namespace

Started start() {
    domain = "amberedit";
    const char* directory = catalogDirectory();
    ::bindtextdomain(domain.c_str(), directory);
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
    ::setenv("LANGUAGE", "en", 1);
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
