#pragma once

#include <string>
#include <string_view>

namespace amberedit::encoding {

/// Works out a message's character set: the CHRS/CHARSET/CODEPAGE kludge in
/// the body (FTS-5003 / FSC-0054), and a default for a message that names none
/// — or names one that identifies no particular encoding, as "IBMPC" does.
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
    ///        a rule). One that names nothing usable falls back to CP866.
    explicit CharsetDetector(std::string_view defaultCharset = "CP866");

    /// The default in force, as the constructor settled it: aliases resolved,
    /// and a name identifying no particular encoding replaced by CP866.
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
    /// last word. Empty when the value names no particular encoding, "IBMPC"
    /// being the one such name in use; the caller falls back to its default.
    static std::string normalize(std::string_view chrsValue);

    /// Whether the value names an encoding that can actually be converted from
    /// — false for "IBMPC" and for an empty value alike.
    static bool namesSpecificCharset(std::string_view chrsValue);

private:
    std::string defaultCharset_;
};

}  // namespace amberedit::encoding
