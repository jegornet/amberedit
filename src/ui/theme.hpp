#pragma once

#include <cstdint>
#include <string>

#include "support/error.hpp"
#include "ui/term/color.hpp"

namespace amberedit::ui::theme {

/// A color in a theme is a number in the terminal's 256-color palette, which is
/// what `term::Color` holds — there is no separate theme color type and nothing
/// is converted anywhere.
using Color = term::Color;

/// The palette used when the config names no theme.
///
/// Twenty-five constants for the thirty-nine roles below. Each is named after
/// the first role that takes it, so that the roles sharing one — and there are
/// several — are visible here rather than only in a theme file that repeats the
/// number.
///
/// themes/black.cfg is this palette written out, and the two are held
/// together by a test: change one and change the other.
///
/// Nothing here is an entry below 16. Those sixteen are whatever the terminal
/// was configured to draw them as, so a palette written in them would change
/// with the profile it is looked at through; every color below is one the
/// 256-color palette fixes.
namespace builtin_theme {

inline constexpr Color kBackground{232};  // #080808, near-black
inline constexpr Color kSelection{25};    // #005faf, a lit blue bar
/// White, and the brightest thing here — which is why so much takes it: what is
/// written on the selection bar and on the field the typing is in, a name that
/// is the user's own, a search hit, and the tenth of a second a button lights up
/// for.
inline constexpr Color kSelectionText{231};  // #ffffff, white
/// A step above the screen, which is all a field needs to read as a box that
/// takes typing: enough to see the slot, not enough to shout about it. A modal
/// box stands on the same step, and for the same reason — near the screen's own
/// color, far enough off it to be seen as laid over it.
inline constexpr Color kInputField{234};  // #1c1c1c, a step above black
inline constexpr Color kInputText{251};   // #c6c6c6, light grey
/// The field the typing is in, lit by a fill two steps further up rather than by
/// a color of its own; the scrollbar's thumb is the same grey, being the moving
/// part against a track that does not move.
inline constexpr Color kFocusedField{241};  // #626262, mid grey
/// The underscores in the room a field has left, and the slot a field inside a
/// box is cut into: both are a step off the fill they stand on and neither is
/// meant to be read, only seen.
inline constexpr Color kInputFiller{237};  // #3a3a3a, near-black
/// The plain light grey: what a box is written in, the buttons of the context
/// menu standing on its fill, and the reader's header block out on the screen.
inline constexpr Color kDialogText{252};  // #d0d0d0, light grey
/// The brightest grey here, for the label standing in a box's top rule. The
/// screen's own heading row is `kTableHeader` — a box is a small enough thing
/// that its title is told from its text by being brighter, where a screen has
/// the room for a hue.
inline constexpr Color kDialogTitle{255};  // #eeeeee, all but white
inline constexpr Color kDialogLabel{111};  // #87afff, light blue
/// The quiet grey: the keys along a box's bottom rule, the buttons in the
/// screen's corners, whatever is shown but will not be acted on, and the hint
/// bar along the last row.
inline constexpr Color kDialogHint{244};  // #808080, mid grey
/// The frame round a box. Between the fill it stands on and the text inside it,
/// so the box is drawn plainly without the frame being the thing that is read.
inline constexpr Color kDialogBorder{240};  // #585858, dark grey
/// What a box casts on the screen behind it: the only thing darker than this
/// palette's own background, which is what a shadow on a screen this dark comes
/// to. A theme on paper puts a grey here instead.
inline constexpr Color kDialogShadow{16};  // #000000, black
/// Message text, and the brightest thing on the screen that is not a heading:
/// what the whole program is there to show is a step over the header block it
/// stands under.
inline constexpr Color kText{254};  // #e4e4e4, off-white
/// #00d700, green — not one of the colors above, a row nobody has read yet
/// being the one thing in the message list that has to be seen without being
/// looked for, where every other role in it is either the text color or the
/// red of a message that has not gone out.
inline constexpr Color kMsglistUnread{40};
inline constexpr Color kLink{33};        // #0087ff, blue
inline constexpr Color kQuoteEven{231};  // #ffffff, white
inline constexpr Color kQuoteOdd{228};   // #ffff87, light yellow
inline constexpr Color kKludge{242};     // #6c6c6c, dark grey
inline constexpr Color kTrailer{249};    // #b2b2b2, light grey
/// The line at the top of every screen. A light blue rather than a grey: the
/// headings name what is under them instead of being part of it, and a hue
/// nothing else on the screen carries says so without another step of white.
inline constexpr Color kTableHeader{110};  // #87afd7, light blue
inline constexpr Color kSeparator{239};    // #4e4e4e, dark grey
inline constexpr Color kError{196};        // #ff0000, red
/// A message written here that has not gone out yet. Softer than `kError`: it is
/// a state the user put the message in rather than something that went wrong.
inline constexpr Color kUnsent{210};  // #ff8787, salmon
/// Behind the message the reader's sidebar marks: a step above the screen's own
/// black, dark enough that the panel does not compete with the message beside it
/// and light enough that the bar is there to be seen. A fill and nothing else —
/// it is never written in, `selection_text` being what stands on it.
inline constexpr Color kSidebarSelection{236};  // #303030, two steps above black

}  // namespace builtin_theme

/// Every color the interface draws with, one field per role, so that a theme
/// can separate roles the built-in palette happens to give the same color.
/// The field names are the theme file's keys, in snake_case.
///
/// A theme is colors and, where a color cannot say it, a switch: a `bool` field
/// is a setting of the look rather than a role, and stands beside the color it
/// belongs to rather than apart from it.
struct Palette {
    /// Painted across the whole screen, so a theme holds together whatever the
    /// terminal's own background is.
    Color background = builtin_theme::kBackground;
    /// Behind whatever Enter would act on: the current row of a list, the
    /// selected button of the quit dialog.
    Color selection = builtin_theme::kSelection;
    /// Written on that fill. Its own role rather than the message text reused:
    /// a theme that selects with a light fill needs something darker here.
    Color selectionText = builtin_theme::kSelectionText;
    /// Behind the message the reader's sidebar marks. A second selection fill
    /// because the panel is marking rather than choosing: the message is the one
    /// on the screen beside it, the keyboard is in the reader and never in the
    /// panel, and a bar as loud as `selection` would put two chosen rows on one
    /// screen and leave the eye to work out which of them Enter would act on.
    ///
    /// A dark step above the screen's own background here, and the quiet
    /// `dimmed` grey in the other themes that ship: what the role asks of a
    /// theme is a fill the eye passes over on its way to the message, and which
    /// end of the ramp that is depends on where the theme's own background sits.
    /// What is written on it is `selection_text`, as on the bar in the lists —
    /// every theme picks something near-white there.
    Color readerSidebarMsglistSelected = builtin_theme::kSidebarSelection;
    /// Behind a field that is typed into but is not the one being typed into
    /// now: the header block of a message being written. A fill rather than a
    /// border, which would cost a column on each side of every field and a row
    /// above and below the block.
    Color inputField = builtin_theme::kInputField;
    /// Written on that fill: a step under the `header` the block around it is
    /// written in, so that a field standing idle reads as part of the header
    /// and still as a box waiting for something. Its own role so that a theme
    /// whose `input_field` is far from its background can pick something
    /// legible on it without moving the rest of the block.
    Color inputText = builtin_theme::kInputText;
    /// Behind the field the typing is actually in, and behind the one stop of
    /// that ring which is a button rather than a field — the attributes under
    /// the addresses. Its own role rather than `selection` reused: the built-in
    /// palette lights the field the typing is in with a step of grey and marks
    /// the row a list would act on with a blue bar, and a theme is free to say
    /// both with one color instead.
    Color focusedField = builtin_theme::kFocusedField;
    /// Written on that fill, `selection_text`'s counterpart for the same
    /// reason: a theme that lights the focused field with a fill of its own
    /// needs to choose what goes on it.
    Color focusedText = builtin_theme::kSelectionText;
    /// The underscores standing in the columns of a field nothing has been
    /// typed into yet — the room it still has, said the way a paper form says
    /// it. Quiet by design: they are the shape of the field rather than
    /// anything the user wrote, and a field with something in it has to read as
    /// what is in it. One color for both states, since an idle field and the
    /// one the typing is in stand side by side on the same screen. The same
    /// role inside a box is `dialog_hint`, which is that palette's quiet color
    /// already.
    Color inputFiller = builtin_theme::kInputFiller;
    /// Whether those underscores are drawn at all — `input_filler_show`, `on` or
    /// `off`. On by default: the built-in palette's fills are steps of
    /// near-black, and the underscores are what says a field is a field before
    /// anything is typed into it. A theme that lights its idle fields plainly
    /// enough on its own turns them off, as themes/blue.cfg does, and
    /// `input_filler` is then a color nothing draws with.
    bool inputFillerShown = true;
    /// Behind a modal box, from its frame to the far corner — the fill wiped
    /// over whatever the box stands on. Its own role rather than `background`
    /// reused: a dialog is meant to read as something laid over the screen, and
    /// it is a step off the screen's own color that says so.
    Color dialogBackground = builtin_theme::kInputField;
    /// The text inside a box: what it asks, what a row of its list says, and
    /// every cell it puts no color of its own on — the margins either side of a
    /// row, a blank line between blocks. The screen's own `text` is not reused,
    /// and the four roles below are not the screen's either: a box carries a
    /// fill of its own, so what is legible on the screen need not be legible on
    /// it. A theme whose dialogs are lighter than its screens — a grey DOS
    /// window over a black one — turns on being able to say both.
    Color dialogText = builtin_theme::kDialogText;
    /// The label in the top rule of a box, naming what it is for.
    Color dialogTitle = builtin_theme::kDialogTitle;
    /// A label inside a box, against the value it names: the From/To/Subj of a
    /// message being answered or forwarded, the fields of the import and export
    /// dialogs, the headings of the info box.
    Color dialogLabel = builtin_theme::kDialogLabel;
    /// The quiet things in a box: the keys along its bottom rule, and whatever
    /// it shows but will not act on — an area that cannot be opened, a menu
    /// button whose command is not available on the message in front of the
    /// user. The screen's `screen_buttons` and `dimmed` are one color in every
    /// shipped theme and this is their counterpart inside a box, for the same
    /// reason they are: quiet is the point of all three.
    Color dialogHint = builtin_theme::kDialogHint;
    /// Behind a field inside a box that is typed into but not being typed into
    /// now — what `input_field` is on a screen, and a role of its own for the
    /// same reason the rest of this family is: the box's fill is not the
    /// screen's, so the slot that has to stand off it need not be either. What
    /// is written on it is `dialog_label`, and the field the typing is actually
    /// in takes `selection`/`selection_text` as everything else does.
    Color dialogField = builtin_theme::kInputFiller;
    /// What a button in a box says while a click on it is being shown — the
    /// confirmation's Yes and No, a button of the menu behind the corner. It
    /// has to be seen against the box's own fill **and** against `selection`,
    /// since a click lands as readily on the selected button as on the other
    /// one.
    Color dialogFlash = builtin_theme::kSelectionText;
    /// The frame round a box — its four sides, the rules that close it top and
    /// bottom, and the dividers between its blocks. `separator`'s counterpart
    /// inside a box, and a role of its own for the family's usual reason: the
    /// rules on a screen are drawn on the screen's fill and this one on the
    /// box's, which a theme may want a different shade against. Only the labels
    /// standing in the rules are lit — `dialog_title` at the top, `dialog_hint`
    /// or `error` at the bottom.
    Color dialogBorder = builtin_theme::kDialogBorder;
    /// The shadow a box casts on what it covers — two columns to the right of it
    /// and one row below, the offset a shadow takes when a cell is twice as tall
    /// as it is wide. A fill and nothing else: the strips it falls on are wiped
    /// to a blank in it, so the screen underneath does not read as text lit from
    /// behind. Its own role because it is the one color drawn *outside* a box
    /// that still belongs to it, and because how far a theme is willing to
    /// darken what is behind a dialog is the theme's business.
    Color dialogShadow = builtin_theme::kDialogShadow;
    /// The message header block — the From/To/Subj labels and their values. A
    /// step under `text`: the block says who wrote what to whom, and the
    /// message itself is what is being read.
    Color header = builtin_theme::kDialogText;
    /// A From or To naming the user themselves.
    Color ownName = builtin_theme::kSelectionText;
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
    /// Service text: kludge lines, the scrollbar's thumb, an area the area list
    /// shows but will not open, and the stand-in `arealist_description_default`
    /// puts where an area describes itself with nothing, which is the program
    /// talking as a kludge line is. Inside a box the counterpart is
    /// `dialog_hint`.
    ///
    /// `screen_buttons` is the quiet buttons drawn straight on the screen: the
    /// back button in the top-left corner, the menu button in the top-right one,
    /// and the delete-line button that walks down the editor's right edge beside
    /// the cursor's row. All three light up in `animated_button_text` while a
    /// click on them is being shown. A button inside a box is not one of them —
    /// that is `menu_button`, on the box's own fill.
    Color kludge = builtin_theme::kKludge;
    Color screenButtons = builtin_theme::kDialogHint;
    Color dimmed = builtin_theme::kDialogHint;
    Color scrollThumb = builtin_theme::kFocusedField;
    /// Quiet but a step brighter: the tearline and origin closing a message,
    /// and the line at the top of every screen — the lists' column headings and
    /// the title over a message being read are one role, so that the top of the
    /// interface reads the same wherever the user is. A box's own top line is
    /// `dialog_title`.
    ///
    /// `menu_button` is the buttons of the context menu, which stand inside a
    /// box and so take the box's fill: it has to be legible on
    /// `dialog_background` and stand apart from `dialog_hint`, which is what a
    /// button that cannot be pressed is drawn in.
    Color trailer = builtin_theme::kTrailer;
    Color tableHeader = builtin_theme::kTableHeader;
    Color menuButton = builtin_theme::kDialogText;
    /// The hint bar along the last row of the screen — the commands of whichever
    /// screen is up. Quiet on purpose: the row is a reminder, not something to
    /// read every frame. What is left of the row is a rule, drawn in
    /// `separator` like every other rule in the interface.
    Color hintBar = builtin_theme::kDialogHint;

    /// Drawn rather than written: the rules a screen sets its blocks apart
    /// with, and the scrollbar's track. A box's own frame is `dialog_border`.
    Color separator = builtin_theme::kSeparator;
    Color scrollTrack = builtin_theme::kSeparator;
    /// Something to see to rather than to read: a screen that could not be
    /// drawn, and a message written here that has not gone out yet.
    Color error = builtin_theme::kError;
    Color unsent = builtin_theme::kUnsent;
    /// Behind every occurrence of what the reader was told to find, in the
    /// message a search landed on — the body and the header block alike. It is
    /// a **fill**, and what is written on it is `background`: a search
    /// highlight has to be seen at a glance from anywhere in a long message,
    /// and a foreground alone would have to compete with the quote colors, the
    /// links and whatever a message's own BBS codes asked for. One role rather
    /// than a pair, the screen's own background being what is legible on
    /// anything bright enough to serve here.
    Color found = builtin_theme::kSelectionText;
    /// What a button on a screen says while a click on it is being shown — the
    /// Back button's arrow, a thread marker, the menu button in the corner. The
    /// frame around a label goes with it, so the whole button is what lights
    /// up; only the color changes, so nothing moves under the pointer. A button
    /// inside a box takes `dialog_flash`, which has the box's fill under it
    /// rather than the screen's.
    Color animatedButtonText = builtin_theme::kSelectionText;
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
/// Fails, naming the file and the key, if it cannot be read or parsed, if a key
/// is not a role, or if a value is not a color. A theme is asked for explicitly,
/// so a mistake in one is worth saying out loud rather than passing over in
/// silence.
[[nodiscard]] tl::expected<Palette, ErrorPtr> loadPalette(const std::string& path);

/// Parses a theme from a string — the entry point used by the tests.
[[nodiscard]] tl::expected<Palette, ErrorPtr> parsePalette(
    const std::string& text, const std::string& originName = "<string>");

}  // namespace amberedit::ui::theme
