#pragma once

#include <cstdint>
#include <string>

#include "ui/term/color.hpp"

namespace amberedit::ui::theme {

/// A color in a theme is a number in the terminal's 256-color palette, which is
/// what `term::Color` holds — there is no separate theme color type and nothing
/// is converted anywhere.
using Color = term::Color;

/// The palette used when the config names no theme.
///
/// Fifteen constants for the twenty-five roles below. Each is named after the first
/// role that takes it, so that the roles sharing one — and there are several —
/// are visible here rather than only in a theme file that repeats the number.
///
/// themes/default.cfg is this palette written out, and the two are held
/// together by a test: change one and change the other.
namespace builtin_theme {

inline constexpr Color kBackground{17};   // #00005f, deep navy
inline constexpr Color kSelection{25};    // #005faf, a lit blue bar
/// One step off the background, which is all a field needs to read as a box
/// that takes typing: enough to see the slot, not enough to shout about it.
inline constexpr Color kInputField{18};   // #000087, a deeper navy
inline constexpr Color kHeader{75};       // #5fafff, bright sky
/// The same shade as the header at present, kept apart so that either may be
/// moved without the other.
inline constexpr Color kLink{75};         // #5fafff
inline constexpr Color kText{253};        // #dadada, off-white
inline constexpr Color kOwnName{253};     // #dadada, off-white
/// #5fafaf, muted cyan — not one of the colors above, a row nobody has read yet
/// being the one thing in the message list that has to be seen without being
/// looked for, where every other role in it is either the text color or a
/// warning.
inline constexpr Color kMsglistUnread{73};
inline constexpr Color kQuoteEven{178};   // #d7af00, gold
inline constexpr Color kQuoteOdd{186};    // #d7d787, pale gold
inline constexpr Color kKludge{102};      // #878787, grey
inline constexpr Color kTrailer{144};     // #afaf87, stone
inline constexpr Color kSeparator{239};   // #4e4e4e, dark grey
/// The hint bar along the bottom. Dark grey in both themes rather than each
/// theme's own quiet color: the row is there to be read past, and a bar that
/// changed shade with the theme would be one more thing drawing the eye down to
/// it.
inline constexpr Color kHintBar{8};  // dark grey, the terminal's own
inline constexpr Color kWarning{174};     // #d78787, dusty red
/// Brighter than anything else here, which is the point: it is on screen for a
/// tenth of a second and has to be seen in that time.
inline constexpr Color kAnimatedButtonText{231};  // #ffffff, white

}  // namespace builtin_theme

/// Every color the interface draws with, one field per role, so that a theme
/// can separate roles the built-in palette happens to give the same color.
/// The field names are the theme file's keys, in snake_case.
struct Palette {
    /// Painted across the whole screen, so a theme holds together whatever the
    /// terminal's own background is.
    Color background = builtin_theme::kBackground;
    /// Behind whatever Enter would act on: the current row of a list, the
    /// selected button of the quit dialog.
    Color selection = builtin_theme::kSelection;
    /// Written on that fill. Its own role rather than the message text reused:
    /// a theme that selects with a light fill needs something darker here.
    Color selectionText = builtin_theme::kText;
    /// Behind a field that is typed into: the header block of a message being
    /// written. A fill rather than a border, which would cost a column on each
    /// side of every field and a row above and below the block. The one the
    /// typing is in takes `selection` instead, as everything else that has it
    /// does.
    Color inputField = builtin_theme::kInputField;
    /// The message header block — the From/To/Subj labels and their values.
    Color header = builtin_theme::kHeader;
    /// A From or To naming the user themselves.
    Color ownName = builtin_theme::kOwnName;
    /// A message in the message list that has not been read yet, across the
    /// row — the number and the date as well as the three text columns, a
    /// message being unread having nothing to do with any one column of it.
    /// A From or To naming the user keeps `own_name`: that cell is about the
    /// name in it rather than about the message, and the rest of the row still
    /// says the message is unread.
    ///
    /// The message list and nowhere else: the reader is where a message is read,
    /// so by the time one is on that screen it has been. What counts as unread
    /// is the base's own mark — see `domain::MessageHeader::seen` — and
    /// `highlight_unread` is what decides whether this is used at all.
    Color msglistUnread = builtin_theme::kMsglistUnread;
    /// Message text.
    Color text = builtin_theme::kText;
    /// A link inside it. Only the address itself takes this color, the rest of
    /// the line keeping whatever it had — a link in a quote stays in the quote.
    Color link = builtin_theme::kLink;
    /// Quoted text by nesting depth, alternating so that several rounds of
    /// reply stay apart.
    Color quoteEven = builtin_theme::kQuoteEven;
    Color quoteOdd = builtin_theme::kQuoteOdd;
    /// Service text: kludge lines, the Back button in the corner, the
    /// scrollbar's thumb, the two things the interface shows but will not act
    /// on — an area that cannot be opened, and a menu button whose command is
    /// not available on the message in front of the user — and the stand-in
    /// `arealist_description_default` puts where an area describes itself with
    /// nothing, which is the program talking as a kludge line is.
    Color kludge = builtin_theme::kKludge;
    Color footer = builtin_theme::kKludge;
    Color dimmed = builtin_theme::kKludge;
    Color scrollThumb = builtin_theme::kKludge;
    /// Quiet but a step brighter: the tearline and origin closing a message,
    /// the line at the top of every screen — the lists' column headings and
    /// the title over a message being read are one role, so that the top of the
    /// interface reads the same wherever the user is — and the buttons of the
    /// context menu, which frame it the same way.
    ///
    /// A menu button has to stand apart from `dimmed`, which is what a disabled
    /// one is drawn in: a theme moving this onto the same color would leave the
    /// two telling nothing apart.
    Color trailer = builtin_theme::kTrailer;
    Color tableHeader = builtin_theme::kTrailer;
    Color menuButton = builtin_theme::kTrailer;
    /// The hint bar along the last row of the screen — the commands of whichever
    /// screen is up. Quiet on purpose: the row is a reminder, not something to
    /// read every frame. What is left of the row is a rule, drawn in
    /// `separator` like every other rule in the interface.
    Color hintBar = builtin_theme::kHintBar;

    /// Drawn rather than written: the rules between blocks, the scrollbar's
    /// track.
    Color separator = builtin_theme::kSeparator;
    Color scrollTrack = builtin_theme::kSeparator;
    /// Something to see to rather than to read: a failed operation, a message
    /// written here that has not gone out yet.
    Color warning = builtin_theme::kWarning;
    Color error = builtin_theme::kWarning;
    Color unsent = builtin_theme::kWarning;
    /// Behind every occurrence of what the reader was told to find, in the
    /// message a search landed on — the body and the header block alike. It is
    /// a **fill**, and what is written on it is `background`: a search
    /// highlight has to be seen at a glance from anywhere in a long message,
    /// and a foreground alone would have to compete with the quote colors, the
    /// links and whatever a message's own BBS codes asked for. One role rather
    /// than a pair, the screen's own background being what is legible on
    /// anything bright enough to serve here.
    Color found = builtin_theme::kText;
    /// What a button says while a click on it is being shown — the Back button's
    /// arrow, the confirmation's Yes and No, a thread marker, the menu button in
    /// the corner and a button of the menu it opens. The frame around
    /// a label goes with it, so the whole button is what lights up; only the
    /// color changes, so nothing moves under the pointer.
    Color animatedButtonText = builtin_theme::kAnimatedButtonText;
};

/// The palette in force.
///
/// Written once, before the screen opens, and only read afterwards — which is
/// what makes a single global bearable here. Every screen draws from it, and
/// threading it through each of them would add a parameter to every render
/// function to describe something that cannot change while they run.
inline Palette palette;

/// Reads a theme file: a line of `role <0..255>` per color, in the same format
/// the AmberEdit config is written in — the roles being Palette's fields in
/// snake_case and the numbers entries in the terminal's own 256-color palette.
/// Every role is optional and an absent one keeps its built-in color, so a file
/// may state as little as one line.
///
/// Throws std::runtime_error naming the file and the key if it cannot be read
/// or parsed, if a key is not a role, or if a value is not a color. A theme is
/// asked for explicitly, so a mistake in one is worth saying out loud rather
/// than passing over in silence.
[[nodiscard]] Palette loadPalette(const std::string& path);

/// Parses a theme from a string — the entry point used by the tests.
[[nodiscard]] Palette parsePalette(const std::string& text,
                                   const std::string& originName = "<string>");

}  // namespace amberedit::ui::theme
