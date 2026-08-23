#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "config/cfg_file.hpp"

namespace amberedit::config {

/// What a row of a list holds is written the same way for both lists —
/// `arealist_format` and `msglist_format` — and this is where that way lives.
/// The letters differ and what they show differs; the shape of the value does
/// not, and a rule that held for one list and not the other would be a rule
/// nobody could remember.
///
/// A format is letters, each optionally followed by the width in columns it is
/// given, with a space standing for itself and `\n` — a backslash and an `n`,
/// the config file knowing no escapes of its own — beginning the next line of
/// the row. What comes back is that value read into lines of fields; which
/// column a letter names is the caller's to say.

/// The width of a field that works its own width out from what it holds rather
/// than from what the format says — the message list's number column, as wide
/// as the highest number that can go in it, and its Date column, as wide as the
/// stamps in it come to. A format may still name a width and override it; there
/// is simply no number to write as the default.
constexpr int kAutoWidth = -1;

/// One field of a format as it was written: the letter, lowercased, and the
/// width it stands in — the one written after the letter, or the letter's own
/// default where none was. A space is the letter `' '` a column wide.
struct ListFormatField {
    char letter{' '};
    int width{0};

    friend bool operator==(const ListFormatField& a, const ListFormatField& b) {
        return a.letter == b.letter && a.width == b.width;
    }
};

/// One line of a row: the fields on it, in the order they were written.
using ListFormatLine = std::vector<ListFormatField>;

/// A whole row: the lines it is drawn on. Never empty — a format written
/// without `\n` in it is the one line every row used to be.
using ListFormatRow = std::vector<ListFormatLine>;

/// A letter a format may be written with, and how wide its field stands where
/// no width is written after it. `kAutoWidth` for a field that works its own
/// width out from what it holds.
struct ListFormatLetter {
    char letter{' '};
    int width{0};
};

/// What both settings say about the value they take, so that the two say it in
/// the same words: which letters there are, and a format to name in a complaint.
struct ListFormatSpec {
    /// The setting's own name, which every complaint opens with.
    std::string_view setting;
    std::vector<ListFormatLetter> letters;
    /// The letters and what they show, as a complaint about an unknown one
    /// lists them: `a number, e echoid, …`.
    std::string_view fields;
    /// A format worth writing, and the wide window's beside it — what an
    /// example in a complaint is built from. The defaults, in practice.
    std::string_view example;
    std::string_view wideExample;
};

/// Reads one format value into the lines of fields it names.
///
/// A row stands at most `kMaxFormatLines` lines tall and every line has to hold
/// something: a row that is to have a blank line in it writes that line as a
/// space, where an empty line is a `\n` typed once too often and a row silently
/// a line taller is a costly way to find that out.
///
/// The letters are read case-insensitively, as `arealist_sort`'s are. A field
/// written twice is not refused: a format is a layout, and there is no reason
/// the same number may not stand at both ends of a row.
[[nodiscard]] Result<ListFormatRow> parseListFormat(const CfgEntry& entry,
                                                    const ListFormatSpec& spec,
                                                    const std::string& value);

/// A row stands at most this many lines tall. Past any format worth writing —
/// an area or a message has five lines' worth to say about itself and no more —
/// and short enough that a `\n` typed once too often is caught while the config
/// is read rather than leaving the list one row to a screen.
constexpr int kMaxFormatLines = 5;

/// The two formats one line names: the narrow window's and the wide one's, the
/// line either side of `adaptive_ui_threshold`. A line with one format on it
/// gives both, which is what every config written before the second value
/// existed says.
///
/// Each format is one value, so a format with a space in it — and a gap between
/// two columns is only ever a space — is written in quotes. That is why a third
/// value is refused rather than joined onto the second: `arealist_format e c un`
/// is three values, and there is no telling it from three formats except by
/// saying that three formats is not a thing to write.
struct ListFormats {
    ListFormatRow narrow;
    ListFormatRow wide;
};

[[nodiscard]] Result<ListFormats> parseListFormats(const CfgEntry& entry,
                                                   const ListFormatSpec& spec);

}  // namespace amberedit::config
