#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace amberedit::encoding {

/// `utf8` with every Cyrillic letter `carried` says no to spelt out in Latin:
/// `Привет` as `Privet`, `Жук` as `Zhuk`, `ЖУК` as `ZHUK`.
///
/// iconv's `//TRANSLIT` is no help here: glibc writes a question mark for a
/// Cyrillic letter, macOS refuses it, and which of them a machine does decides
/// what a message written into CP437 or LATIN-1 says. This is the one answer.
///
/// `carried` is asked with the UTF-8 bytes of one letter, and a letter it says
/// yes to is left as it stands — so a charset holding Cyrillic, CP866 or KOI8-R,
/// gets its own letters and loses nothing. Spelt out are the Russian,
/// Ukrainian and Belarusian letters, the Serbian, Macedonian and Bulgarian ones,
/// and those of the Turkic languages written in Cyrillic (and Tajik, which
/// shares them); any other character is left for the conversion that follows
/// to deal with.
///
/// A letter is spelt one way whatever language it stands in, since nothing in
/// the text says which language that is: a letter Russian shares is spelt the
/// Russian way, Bulgarian щ as `sch` and not `sht`.
///
/// A capital spelt with two letters keeps the case of the word it stands in:
/// `Zh` beside lower case, `ZH` beside another capital.
[[nodiscard]] std::string transliterated(
    std::string_view utf8, const std::function<bool(std::string_view)>& carried);

}  // namespace amberedit::encoding
