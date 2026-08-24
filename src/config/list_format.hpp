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
/// the row. A letter whose field is written by a format of its own may carry one
/// in brackets after the width. What comes back is that value read into lines of
/// fields; which column a letter names, and what a format in brackets has to
/// look like, are the caller's to say.

/// The width of a field that works its own width out from what it holds rather
/// than from what the format says — the message list's number column, as wide
/// as the highest number that can go in it, and its Date column, as wide as the
/// stamps in it come to. A format may still name a width and override it; there
/// is simply no number to write as the default.
constexpr int kAutoWidth = -1;

/// One field of a format as it was written: the letter, lowercased, the width it
/// stands in — the one written after the letter, or the letter's own default
/// where none was — and whatever stood in brackets after that. A space is the
/// letter `' '` a column wide.
struct ListFormatField {
    char letter{' '};
    int width{0};
    /// What the brackets held, as they held it, and empty where none were
    /// written. Only a letter the spec marks may carry one; what it means is the
    /// caller's business, this having read it and nothing more.
    std::string format;

    friend bool operator==(const ListFormatField& a, const ListFormatField& b) {
        return a.letter == b.letter && a.width == b.width && a.format == b.format;
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
    /// Whether `d(...)` is a thing to write after this letter. False for every
    /// field that is drawn from what it holds and nothing else, which is most
    /// of them.
    bool takesFormat{false};
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
    /// The letters that take a format in brackets, as a complaint about one
    /// that does not names them: `d date`. Empty where no letter does, and the
    /// complaint then says so instead.
    std::string_view formatted;
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
///
/// A letter the spec marks may be followed by a format of its own in brackets,
/// after its width where one is written: `d(%d %b %y)`, `d15(%d %b %y)`.
/// Everything up to the first `)` is that format, spaces and all — the value is
/// one quoted string already, so a space in there is no more trouble than a
/// space between two fields. Brackets that are never closed, brackets with
/// nothing in them, a width written after them instead of before, and brackets
/// after a letter that takes none are all refused rather than read past: each is
/// a format the user meant something by.
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

/// The one format a setting that has no two sides to it takes — the reader's
/// sidebar, which is only ever on the screen in a window wide enough for it and
/// so has no narrow window to name a second format for.
///
/// A second value is refused rather than read as the wide window's: a setting
/// that took two everywhere else and one here would be a rule nobody could
/// remember, and a format with a space in it is written in quotes either way.
[[nodiscard]] Result<ListFormatRow> parseOneListFormat(const CfgEntry& entry,
                                                       const ListFormatSpec& spec);

}  // namespace amberedit::config
