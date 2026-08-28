#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amberedit::config {

/// Everything AmberEdit can be asked to do, and the whole of what a key, a menu
/// button or a hint may name.
///
/// **Only what runs a command is here.** Moving about — the arrows, PgUp and
/// PgDn, Home and End, Space, Enter, Esc, Backspace and Tab — is the same on
/// every screen and in every dialog, and is neither bindable nor nameable: a
/// layout that had dropped Esc would be a layout with no way out of the screen
/// it left you on. The dialogs answer for themselves for the same reason.
enum class Command : uint8_t {
    AppQuit,                ///< app.quit
    AreaListNextUnread,     ///< arealist.next_unread
    AreaListToggleUnread,   ///< arealist.toggle_unread
    AreaListRescan,         ///< arealist.rescan
    MessageListMarkToggle,  ///< msglist.mark_toggle
    ReaderReply,            ///< reader.reply
    ReaderReplyElsewhere,   ///< reader.reply_elsewhere
    ReaderCommentReply,     ///< reader.comment_reply
    ReaderNew,              ///< reader.new
    ReaderForward,          ///< reader.forward
    ReaderChange,           ///< reader.change
    ReaderDelete,           ///< reader.delete
    ReaderExport,           ///< reader.export
    ReaderFind,             ///< reader.find
    ReaderList,             ///< reader.list
    ReaderInfo,             ///< reader.info
    ReaderNodelist,         ///< reader.nodelist
    ReaderKludges,          ///< reader.kludges
    ReaderScrollbar,        ///< reader.scrollbar
    ReaderThreadUp,         ///< reader.thread_up
    ReaderThreadDown,       ///< reader.thread_down
    ReaderMarkToggle,       ///< reader.mark_toggle
    ReaderMarkMenu,         ///< reader.mark_menu
    ReaderShell,            ///< reader.shell
    ComposeSave,            ///< compose.save
    ComposeAttributes,      ///< compose.attributes
    ComposeImport,          ///< compose.import
    ComposeHeaderBack,      ///< compose.header_back
    ComposeDeleteLine,      ///< compose.delete_line
    ComposeRestoreLine,     ///< compose.restore_line
    ComposeDeleteQuote,     ///< compose.delete_quote
    ComposeDeleteWord,      ///< compose.delete_word
    ComposeWordLeft,        ///< compose.word_left
    ComposeWordRight,       ///< compose.word_right
    ComposeLineStart,       ///< compose.line_start
    ComposeLineEnd,         ///< compose.line_end
    // The external utilities, ten to a screen and every one of them the same
    // command with another program behind it. They stand together at the end
    // because that is what lets a slot and a screen be worked out from the
    // value — see `Commands::externUtilOf()` — rather than written out thirty
    // times over.
    AreaListExternUtil0,  ///< arealist.extern_util0
    AreaListExternUtil1,
    AreaListExternUtil2,
    AreaListExternUtil3,
    AreaListExternUtil4,
    AreaListExternUtil5,
    AreaListExternUtil6,
    AreaListExternUtil7,
    AreaListExternUtil8,
    AreaListExternUtil9,
    ReaderExternUtil0,  ///< reader.extern_util0
    ReaderExternUtil1,
    ReaderExternUtil2,
    ReaderExternUtil3,
    ReaderExternUtil4,
    ReaderExternUtil5,
    ReaderExternUtil6,
    ReaderExternUtil7,
    ReaderExternUtil8,
    ReaderExternUtil9,
    ComposeExternUtil0,  ///< compose.extern_util0
    ComposeExternUtil1,
    ComposeExternUtil2,
    ComposeExternUtil3,
    ComposeExternUtil4,
    ComposeExternUtil5,
    ComposeExternUtil6,
    ComposeExternUtil7,
    ComposeExternUtil8,
    ComposeExternUtil9,
};

/// One past the last command, which is the width of the table a `KeyMap` keeps.
inline constexpr size_t kCommandCount =
    static_cast<size_t>(Command::ComposeExternUtil9) + 1;

/// How many external utilities a config may name: `extern_util0` through
/// `extern_util9`, and one command per slot on every screen that runs them.
///
/// Ten because a hand of them is what a row of keys holds and a menu can be read
/// down; the digit in the name is the slot, so the count and the last digit are
/// one and the same thing.
inline constexpr size_t kExternUtilCount = 10;

/// Which screen answers a command.
///
/// It is what lets one key mean two things: `F2` is Change in the reader and
/// Save in the editor, and neither screen is ever the other. Two commands of one
/// screen may not share a key, and an `Anywhere` command shares with nothing.
///
/// It is also what a menu or a hint list is read against: `reader_menu save` is
/// refused where it is written rather than being a button that does nothing.
enum class CommandScreen : uint8_t {
    /// Answered before every screen, so it shares a key with nothing.
    Anywhere,
    AreaList,
    MessageList,
    Reader,
    Compose,
};

/// The one list of commands there is: what each is called, where it is answered,
/// how it reads on a button and which keys it runs on when no layout has been
/// named.
///
/// Everything that has to name a command reads it from here — the keyboard
/// (`ui/keys`), the context menu (`reader_menu`, `compose_menu`) and the hint
/// bars (`arealist_hints` and the rest). A second table beside this one is a
/// table that falls out of step with it.
class Commands {
public:
    /// Where a command may be named beyond the layout: not everything a key does
    /// is a thing a button can offer.
    enum class In : uint8_t {
        Menu,     ///< the context menu behind a screen's top-right corner
        HintBar,  ///< the last row of the screen
    };

    /// One command, entire.
    struct Info {
        Command command{};
        /// What a config and a `keys` file call it: `reader.reply_elsewhere`. The
        /// screen in front of the dot is what the two are read against, and
        /// `shortName` is the rest.
        std::string_view name;
        CommandScreen screen{};
        /// The English word a button and a hint are written with — **the msgid,
        /// and not the label to draw**. `labelOf()` is what a button draws; this
        /// is what it asks the catalog with.
        ///
        /// A `const char*` and not a `string_view` like the fields around it,
        /// because that is what a msgid is: `gettext` is handed a C string, and
        /// a view would only have to be turned back into one at the call. The
        /// `Id` in the name is the warning that goes with it — comparing two of
        /// these with `==` compares the pointers, so anything reading it as text
        /// says `std::string_view(info.labelId)`.
        const char* labelId{};
        /// The glyph the menu marks the command with, or empty for a command
        /// that goes without one.
        ///
        /// It is kept apart from the word because it is not the language's — it
        /// says the same thing in every one of them, and a translation that
        /// carried it along would have every translator copying it back in. It
        /// is also the half that cannot be measured by counting: `⚠︎` is two code
        /// points and one column, `𝒊` is four bytes and one column, and an emoji
        /// is one glyph in two columns — and which of them a platform's
        /// `wcwidth()` calls what is the platform's to say. So nothing assumes a
        /// width; `menu_dialog::labelLine()` measures.
        std::string_view icon;
        /// The keys it runs on when no `keys` file has been named, blank
        /// separated and in the order they are to be offered.
        ///
        /// Written as spellings rather than as events so that this table is the
        /// one place the layout is stated. `amberkeys.cfg.example` is the same
        /// table written out, and a test reads that file back through here to
        /// keep the two saying the same thing.
        std::string_view keys;
        /// Whether the context menu may offer it. Everything a key does can be a
        /// hint — a hint is a key with its name beside it — but a menu button is
        /// a thing the screen has to be able to run on its own, and editing the
        /// line the cursor is on is not one of those.
        bool inMenu{false};
    };

    /// Every command, in the order the enumeration is written.
    [[nodiscard]] static const std::vector<Info>& all();

    /// One command, by its own value.
    [[nodiscard]] static const Info& of(Command command);

    /// The word to draw for it, in the interface's own language.
    ///
    /// Apart from `of(command).labelId` because the table is built before the
    /// config that names a catalog has been read — the config itself names
    /// commands, in `reader_menu` and the hint bars — so a translation cached in
    /// the table would be the English one every time. This asks as it draws.
    ///
    /// **What draws asks `AppConfig::labelOf()` rather than this**, an external
    /// utility's word being the `title` its config line gave it and no catalog's.
    [[nodiscard]] static const char* labelOf(Command command);

    /// The part of the name after the screen it belongs to: `reader.reply_elsewhere`
    /// reads as `reply_elsewhere`. What a menu and a hint list name a command by, the
    /// config key already saying which screen is meant.
    [[nodiscard]] static std::string_view shortNameOf(Command command);

    /// The command of that whole name — `reader.reply_elsewhere` — or nothing where no
    /// command is called that. Read without regard to case, and with `-` read as
    /// `_`: `reader.reply-elsewhere` is how the same command was written once,
    /// and a config that still says so is still read.
    [[nodiscard]] static const Info* named(std::string_view name);

    /// The command that screen calls by that short name, or nothing where it
    /// offers none — a screen's own commands and the ones answered everywhere,
    /// and only those `where` can hold. Read as `named()` reads, `-` and `_`
    /// alike.
    [[nodiscard]] static const Info* namedOn(CommandScreen screen, std::string_view name,
                                             In where);

    /// What that screen offers a list, in the order the table is written: what a
    /// setting is read against and what its error message names.
    [[nodiscard]] static std::vector<Command> offeredOn(CommandScreen screen, In where);

    /// Those names, in one line — `list, reply, reply_elsewhere, …`, which is how a
    /// setting says what it would have taken.
    [[nodiscard]] static std::string offeredNamesOn(CommandScreen screen, In where);

    /// Which `extern_utilN` the command runs, or nothing where it is not one of
    /// them at all.
    ///
    /// The slot is the digit in the name and nothing else: `arealist.extern_util3`,
    /// `reader.extern_util3` and `compose.extern_util3` are three commands and
    /// one utility, so the screen a key was pressed on decides nothing about
    /// what runs.
    [[nodiscard]] static std::optional<size_t> externUtilOf(Command command);

    /// That screen's command for the slot, or nothing where the screen runs no
    /// utilities — which the message list does not.
    [[nodiscard]] static std::optional<Command> externUtilOn(CommandScreen screen,
                                                             size_t slot);
};

}  // namespace amberedit::config
