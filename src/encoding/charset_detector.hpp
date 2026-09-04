#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace amberedit::encoding {

/// Works out a message's character set: the CHRS/CHARSET/CODEPAGE kludge in
/// the body (FTS-5003 / FSC-0054), and a default for a message that names none
/// — or names one that identifies no particular encoding, as "IBMPC" does, or
/// writes the kludge in a shape FTS-5003 does not have, as "+7 FIDO 2" does.
///
/// The default comes from AppConfig::defaultCharset, and it is a per-area
/// answer: no tosser config format states one — husky fidoconfig knows no
/// `-charset` — but an AmberEdit area group may, and "IBMPC" means different
/// code pages in different areas, so the per-area answer is the only correct one
/// where it does. A detector belongs to one open message base, and the base is
/// built with the charset that area is read in, so there is nothing to override
/// afterwards.
class CharsetDetector {
public:
    /// @param defaultCharset used when there is no other indication (CP866 as
    ///        a rule). It has to name a charset in particular — the config
    ///        refuses `default_charset IBMPC` at the line that states it, and
    ///        there is nothing to guess here that would not be a guess.
    explicit CharsetDetector(std::string_view defaultCharset);

    /// The default in force, as the constructor settled it: the same name with
    /// its aliases resolved, `+7_FIDO` arriving as CP866.
    [[nodiscard]] std::string defaultCharset() const;

    /// Returns a charset name suitable for iconv_open().
    /// @param rawBody the raw message body, kludges (^A...) included
    [[nodiscard]] std::string detect(std::string_view rawBody) const;

    /// Extracts the value of the CHRS/CHARSET/CODEPAGE kludge, e.g. "CP866 2".
    /// An empty string means there is no such kludge.
    static std::string extractChrsKludge(std::string_view rawBody);

    /// Maps a Fidonet charset name onto something iconv understands:
    /// "+7_FIDO"/"CP866 2" become "CP866", "LATIN-1 2" becomes "ISO-8859-1".
    /// Unknown names are returned as they are — iconv_open gets to have the
    /// last word. Empty when the value names no particular encoding: "IBMPC",
    /// and a value not shaped like "<name> <level>" at all, "+7 FIDO 2" being
    /// the one such spelling in circulation. The caller falls back to its
    /// default for both.
    static std::string normalize(std::string_view chrsValue);

    /// Whether the value names an encoding that can actually be converted from
    /// — false for "IBMPC", for a malformed value and for an empty one alike.
    static bool namesSpecificCharset(std::string_view chrsValue);

private:
    /// Whether this machine's iconv has heard of the name, asked once per name
    /// and remembered: `detect()` runs on every row of a message list, and a
    /// base holds one or two charsets across thousands of messages.
    [[nodiscard]] bool knownToIconv(const std::string& charset) const;

    std::string defaultCharset_;
    mutable std::unordered_map<std::string, bool> known_;
};

}  // namespace amberedit::encoding
