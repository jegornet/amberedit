#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace amberedit::encoding {

/// Where one occurrence stands in the text that was searched: byte offsets into
/// the UTF-8 string handed to findAll(), so that exactly the characters which
/// matched can be painted.
struct TextMatch {
    size_t begin{0};
    size_t end{0};
};

/// Looking for a word in a message, without regard to case, **in the charset
/// the message itself declares** — its CHRS kludge, or the area's
/// `default_charset` where it carries none.
///
/// The charset is what folds the two sides together, and it is emphatically not
/// the locale's: `<cctype>` under a single-byte locale folds the whole high half
/// of the byte range, which would make two differently spelled Cyrillic words
/// compare equal, and under a UTF-8 one it folds nothing above ASCII at all. So
/// the folding here is written out, over code points, and the charset decides
/// what it is allowed to do:
///
/// - **Case** is folded for the alphabets FTN mail is written in — ASCII, the
///   Latin-1 supplement and Cyrillic. Every single-byte charset a message
///   states is a subset of those, so folding the decoded text is the same
///   answer folding the stored bytes would give, and it is also the right
///   answer for a message that states UTF-8.
/// - **CP866 alone carries the Russian language support quirks**: a message written in it may
///   spell Н, р and у with the Latin letters that look the same on a DOS screen
///   — H, p and y — because the two are a keyboard layout apart. Those pairs are
///   folded together, and only there: in a western area they are six different
///   letters.
///
/// The query and the charset are set apart from each other because a search
/// runs over a whole area: the words are typed once and the charset changes
/// message by message, most areas never changing it at all.
class TextSearch {
public:
    TextSearch() = default;
    TextSearch(std::string_view query, std::string_view charset);

    /// What is being looked for, as UTF-8 — the terminal's own encoding, which
    /// is what everything above the message-base port is in.
    void setQuery(std::string_view query);
    /// The charset the text about to be searched is written in, as
    /// `CharsetDetector` names it. Setting the one it already holds costs
    /// nothing, which is what makes calling it per message reasonable.
    void setCharset(std::string_view charset);

    [[nodiscard]] const std::string& query() const { return query_; }
    [[nodiscard]] const std::string& charset() const { return charset_; }

    /// Whether there is nothing to look for. An empty query matches nothing
    /// rather than everything: a search for no words has not been asked.
    [[nodiscard]] bool empty() const { return needle_.empty(); }

    /// Every occurrence in `text`, in order and without overlapping.
    [[nodiscard]] std::vector<TextMatch> findAll(std::string_view text) const;

    /// Whether there is one at all — the same walk, stopped at the first.
    [[nodiscard]] bool contains(std::string_view text) const;

private:
    void refold();
    /// The walk both of the two above are: every occurrence, or the first of
    /// them where that is all the caller wanted — folding the text is the whole
    /// cost, and a scan over an area asks only whether there was one.
    [[nodiscard]] std::vector<TextMatch> search(std::string_view text,
                                                bool firstOnly) const;

    std::string query_;
    std::string charset_;
    /// Whether the charset is the one the Russian language support quirks belong to.
    bool quirks_{false};
    /// The query folded down to one unit per character.
    std::vector<char32_t> needle_;
};

}  // namespace amberedit::encoding
