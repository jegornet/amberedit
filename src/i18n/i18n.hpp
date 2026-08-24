#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

namespace amberedit::i18n {

/// The interface's own language, and the whole of how a string reaches a user.
///
/// Every word AmberEdit draws is written here in English and passed through
/// `_()`, which is gettext's own spelling and what `xgettext` looks for. With no
/// catalog loaded — the ordinary case, and every case before the config has been
/// read — `_()` hands the literal straight back.
///
/// **The language is the environment's**, exactly as it is for every other
/// program on the system: `LANGUAGE`, `LC_ALL`, `LC_MESSAGES`, `LANG`, in
/// gettext's own order of preference. `LANG=ru_RU.UTF-8 amberedit` is Russian
/// and nothing has to be written in a config to make it so.
///
/// **Where the catalogs are is compiled in**, because `bindtextdomain()` has to
/// be told and nothing at run time could work it out. Two directories: the
/// build's own first, so a binary that has only been built is already
/// translated, and the install prefix behind it, which is what answers on a
/// machine the build directory never existed on.
///
/// **What comes back is UTF-8 and lives as long as the program.**
/// `bind_textdomain_codeset()` settles the first whatever charset the catalog
/// was compiled in, which is what the rest of the tree requires: everything
/// above `ui/term` is UTF-8 and the terminal layer is what encodes on the way
/// out. The second is libintl's own guarantee, and it is what lets a translation
/// stand where a string literal stood.

/// What settling the language came to.
///
/// Nothing here is a failure. An environment naming no language is English and
/// says so by saying nothing, which is the ordinary case and carries no warning.
/// An environment naming one the system cannot install — a locale that was never
/// generated, which is a stock Debian or Ubuntu — is worth one line, because the
/// user asked for something and did not get it.
struct Started {
    /// The locale `LC_MESSAGES` ended up in, empty where the environment named
    /// none. What a test asserts on; nothing in the interface asks.
    std::string locale;
    /// Empty in every ordinary case. Otherwise what went wrong and what to do
    /// about it, ready to be printed.
    std::string warning;
};

/// Binds the catalogs and settles `LC_MESSAGES` from the environment.
///
/// Called once, before anything is printed. Only `LC_MESSAGES` is touched:
/// `LC_CTYPE` is the terminal layer's, and `term::ensureUtf8Locale()` has its
/// own reasons for what it puts there.
[[nodiscard]] Started start();

/// Puts the interface back into English, whatever it was in before.
///
/// The pair to `start()`, and what makes a language a thing that can be put down
/// again rather than a door that only opens one way. Nothing in the running
/// program calls it — a language is settled at startup and does not change under
/// the user — and a test that has changed one calls it to leave the process as
/// it found it.
void clear();

/// Whether a catalog is answering: whether `_()` is really handing back anything
/// other than what it was given. False under English, which has no catalog.
[[nodiscard]] bool translating();

/// The translation of that message, or the message itself where there is none.
///
/// `msgid` has to outlive the call, which every string literal does and which is
/// the only thing this is ever handed. Written `_()` at the call sites.
[[nodiscard]] const char* translate(const char* msgid);

/// The translation of that message in that context, or the message itself.
///
/// For a word that is two different words in another language: `New` is the
/// unread column of the area list and it is the command that writes a message,
/// and Russian has no one word that is both. The context is not shown to
/// anybody — it reaches the translator as the `msgctxt` of the entry.
[[nodiscard]] const char* translate(const char* context, const char* msgid);

/// The form of that message `n` asks for.
///
/// `one` and `many` are the English singular and plural, and they are what a
/// catalog with no entry for them answers with. How many forms the language has
/// and which one `n` takes are the catalog's to say — Russian has three and
/// picks by the last digit, and nothing here knows that.
[[nodiscard]] const char* plural(const char* one, const char* many, unsigned long n);

/// `pattern` with `{0}`, `{1}` … replaced by the arguments.
///
/// What a message with something of the user's in it is built with, in place of
/// the concatenation it would otherwise be. The two are not the same offer: a
/// translator handed `"cannot open "` and a path to paste after it cannot move
/// the path, and word order is exactly what a translation changes. So the whole
/// sentence is one message with the parts numbered inside it.
///
/// A `{` that is not a placeholder stands as it is, and a placeholder naming an
/// argument that was not passed is left as it was written — a translation is
/// user-supplied text as far as this is concerned, and it must not be able to
/// read past the end of the list.
[[nodiscard]] std::string format(std::string_view pattern,
                                 std::initializer_list<std::string_view> arguments);

}  // namespace amberedit::i18n

/// gettext's own spelling, and what `xgettext` extracts without being told to.
/// It is the one macro in the tree: every other name here is a function, and
/// this exists because it stands in front of several hundred string literals and
/// because the extractor recognises it by name.
// NOLINTNEXTLINE(bugprone-reserved-identifier)
#define _(msgid) ::amberedit::i18n::translate(msgid)

/// The same, with a context in front: `C_("area list", "New")`.
#define C_(context, msgid) ::amberedit::i18n::translate(context, msgid)

/// Marks a literal for extraction without translating it here — for a string
/// stored now and shown later, which is then passed through `_()` at the point
/// it is drawn.
#define N_(msgid) (msgid)
