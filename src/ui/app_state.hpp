#pragma once

#include "ui/term/box.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/area_manager.hpp"
#include "app/compose_prefill.hpp"
#include "app/copy_commands.hpp"
#include "app/export_file.hpp"
#include "app/import_file.hpp"
#include "app/message_builder.hpp"
#include "app/message_search.hpp"
#include "app/navigator.hpp"
#include "config/app_config.hpp"
#include "config/text_util.hpp"
#include "domain/message.hpp"
#include "i18n/i18n.hpp"
#include "nodelist/nodelist_db.hpp"
#include "ports/i_msgbase.hpp"
#include "ui/bbs_codes.hpp"
#include "ui/dir_listing.hpp"
#include "ui/keys.hpp"
#include "ui/text_editor.hpp"
#include "ui/wheel_throttle.hpp"

namespace amberedit::ui {

/// The whole application's state: screens read it when rendering and change
/// it when handling keys. One struct for all three screens is simpler than
/// passing cursors between them.
struct AppState {
    app::AreaManager& manager;

    /// Three configs, and which of them to read is decided by one question:
    /// **may an area group state this setting?**
    ///
    /// - `config` — the file as it was read. Everything a group may not state:
    ///   the layout, the menus, the theme, the AKA rules. Most of the
    ///   interface reads this and nothing else.
    /// - `areaConfig` — the same with the groups covering `currentArea` laid
    ///   over it. Anything a group may state, read while showing the area:
    ///   `userName`, `styleCodes`, `bbsCodesRenegade` and `bbsCodesAnsi` today.
    /// - `composeConfig()` — the same for the area a message is being written
    ///   into, which is not always the one on screen.
    ///
    /// Reading a groupable setting off `config` is the mistake to watch for: it
    /// would work everywhere except in the areas a group covers, which is the
    /// one place the setting was written for.
    const config::AppConfig& config;
    config::AppConfig areaConfig;

    /// Which keys run which commands, from the `keys` file or AmberEdit's own
    /// layout. Every screen asks this rather than comparing against a key of
    /// its own, and the dialogs that close on the key that opened them ask it
    /// too — `i` puts the Info box away because `i` is what opened it, whatever
    /// key that is today.
    KeyMap keys{KeyMap::defaults()};
    app::Navigator navigator;

    int width{80};
    int height{24};

    /// The context menu the button in the top-right corner opens, or nothing
    /// when it is not up — it is modal, and it is built afresh from the config
    /// every time it is asked for, so there is nothing about it worth
    /// remembering between two openings.
    ///
    /// The commands are copied into it rather than read back off the config
    /// while it stands: whether each of them can be run is decided once, when
    /// the menu opens, on the message that was in front of the user then.
    struct MenuView {
        /// One command as it stands in the menu.
        struct Item {
            Command command{};
            /// Whether it can be run on what is in front of the user. A disabled
            /// button is drawn quietly and swallows a click: it is still a
            /// button, and letting the press through to the screen underneath
            /// would be worse than doing nothing.
            bool enabled{true};
            /// Where it was last drawn, filled in by the menu's render() — a
            /// click is tested against what is on the screen rather than against
            /// a second copy of the same arithmetic.
            term::Box box;
        };
        std::vector<Item> items;
        /// Which of them the cursor is on. Never one that cannot be run: the
        /// menu opens on the first that can and steps over the rest.
        int cursor{0};
    };
    std::optional<MenuView> menuView;

    // --- area list --------------------------------------------------------
    int areaCursor{0};
    int areaOffset{0};
    /// How many lines tall a row of the area list stood when `areaOffset` was
    /// last settled, and zero before the list has been drawn at all.
    ///
    /// The two `arealist_format`s may be different heights — the default puts
    /// the description on a line of its own in a narrow window and beside the
    /// name in a wide one — so dragging the window across
    /// `adaptive_ui_threshold` changes how many areas a screen holds. This is
    /// what the screen works the new offset out from, keeping the selected area
    /// where it stands on the screen rather than where it stands in the list.
    int areaRowHeightShown{0};
    /// The area list's quick search: what has been typed of an area's name so
    /// far. Empty means no search is up, and then the table's heading row is
    /// shown in place of the input line — the search has no state of its own
    /// beyond this string.
    std::string areaSearch;
    /// Whether a rescan of the areas has been asked for. Ctrl-R sets it and the
    /// shell clears it, having done the reading: the modal saying what is going
    /// on has to be on the screen before every base is opened again, and that
    /// is what blocks in between.
    bool rescanning{false};
    /// Which area is being read while it runs, as the modal names it. Empty
    /// before the first one is reached, which is the tosser config being read.
    std::string rescanArea;

    // --- message list -----------------------------------------------------
    ports::IMsgBase* base{nullptr};
    domain::AreaConfig currentArea;
    uint32_t messageCount{0};
    /// How many lines tall a row of the message list stood when
    /// `messageOffset` was last settled, and zero before the list has been
    /// drawn at all — what `AppState::areaRowHeightShown` is for the other
    /// list, and settled the same way when the window is dragged across
    /// `adaptive_ui_threshold`.
    int msgRowHeightShown{0};
    int messageCursor{0};
    int messageOffset{0};

    /// Which message stands on the top row of the reader's sidebar, counted
    /// from zero. Its own scrolling position rather than `messageOffset`: the
    /// panel and the list screen are two windows on the same area, of different
    /// heights and with rows of different heights, and neither is ever looking
    /// at what the other is.
    ///
    /// Settled by `reader_sidebar::follow()` as each message is loaded — so it
    /// tracks the message on the screen even in a window too narrow to show the
    /// panel, and widening the window puts it up on the right message.
    int readerSidebarOffset{0};
    /// How many messages the sidebar held when it was last drawn, and zero
    /// before it has been drawn at all. A window resized under the panel is what
    /// this is for: the offset is the user's to scroll where the message being
    /// read is on it already, and a screen made shorter can carry that message
    /// off the panel with nothing having been asked for.
    int readerSidebarItemsShown{0};

    /// A window of loaded headers: keeping a whole area in memory (tens of
    /// thousands of messages) is pointless, and the base reads every header from
    /// disk anyway.
    std::vector<domain::MessageHeader> headers;
    int headersStart{0};

    /// Whether whatever has been typed and not read yet is to be thrown away
    /// rather than acted on. Set by a screen that has just put a different one
    /// in front of the user without being asked to; the shell drops the keys
    /// and clears it, since the terminal is what holds them.
    bool discardTypeahead{false};

    // --- message reader ---------------------------------------------------
    /// Whether the user's own shell has been asked for. The reader sets it and
    /// the shell clears it, having handed the terminal over and taken it back:
    /// a screen has no terminal — `runApp()` is what holds one — and giving it
    /// away is the terminal's own, as `Terminal::handOver()` says.
    bool shellRequested{false};
    std::optional<domain::MessageHeader> readHeader;
    std::optional<domain::MessageBody> readBody;
    /// What the message on screen answers and what answers it, as the title
    /// shows beside its number.
    domain::MessageThread readThread;
    /// One marker of that, and where it was last drawn. Filled in by the
    /// reader's render() so that a click lands on the message the marker names
    /// rather than on a second guess at the same arithmetic.
    struct ThreadLink {
        uint32_t number{0};
        term::Box box;
    };
    std::vector<ThreadLink> readThreadLinks;

    /// One link in the message text, and where it was last drawn. Filled in by
    /// the reader's render() only where `urlhandler` names a program to open
    /// one with: with no handler a click on a link does nothing, and there is
    /// nothing for the frame to remember.
    struct UrlLink {
        std::string url;
        term::Box box;
    };
    std::vector<UrlLink> readUrlLinks;

    /// The link a click landed on, waiting for `urlhandler` to be run on it.
    /// The reader sets it and the shell clears it, having handed the terminal
    /// over and taken it back — the same way `shellRequested` above works, and
    /// for the same reason: a program wanting the screen cannot start while a
    /// frame is being drawn on it.
    std::string urlRequested;

    /// The replies dialog: one row per message answering the one on screen,
    /// with what each row shows already read off its header. Empty when the
    /// dialog is not up — it only comes up where there is a choice to make.
    struct ReplyChoice {
        uint32_t number{0};
        std::string from;
        std::string address;
        std::string date;
        term::Box box;
    };
    std::vector<ReplyChoice> replyChoices;
    int replyChoice{0};

    /// The forward picker: what `m` is to do with the message on screen, asked
    /// before the area it is to be done into. Absent when the dialog is not up —
    /// it is modal, and answering it puts the area picker below up in its place,
    /// which is where all three answers go next.
    struct ForwardPicker {
        /// The three ways a message reaches another area. Only the first writes
        /// anything of the user's: the other two put this very message there,
        /// and differ in nothing but whether it stays here as well.
        enum class Mode {
            Forward,  ///< a new message of ours carrying this one
            Move,     ///< the same message there and gone from here
            Copy,     ///< the same message there and standing here still
        };
        Mode mode{Mode::Forward};
        /// Where each of the three buttons was drawn, filled in by the dialog's
        /// render() so that a click is tested against the frame rather than
        /// against a second copy of its arithmetic.
        term::Box forwardBox;
        term::Box moveBox;
        term::Box copyBox;
    };
    std::optional<ForwardPicker> forwardPicker;

    /// The area picker: which area a message is to be written into, asked by
    /// the reader's `n` and `m`. Absent when the dialog is not up — it is
    /// modal, and there is nothing about it worth remembering between two
    /// askings.
    ///
    /// The areas themselves are not copied into it: they are the manager's own
    /// list, which nothing touches while the dialog stands over the reader.
    struct AreaPicker {
        /// What the area picked is for. The dialog is the same list either way
        /// and says which by its title; what differs is the message begun once
        /// it has been answered.
        enum class For {
            Reply,    ///< answering the message on screen there
            Forward,  ///< passing it on into there, in a message of one's own
            Move,     ///< putting the message itself there and taking it out here
            Copy,     ///< putting the message itself there and leaving it here too
        };
        For purpose{For::Reply};
        int cursor{0};
        int offset{0};
        /// What has been typed of an area's name so far, as the area list's own
        /// quick search works. Empty means no search is up.
        std::string search;
        /// One row on the screen and the area it names, filled in by the
        /// dialog's render() so that a click lands on the row the pointer is
        /// over rather than on a second guess at the same arithmetic.
        struct Row {
            int index{0};
            term::Box box;
        };
        std::vector<Row> rows;
    };
    std::optional<AreaPicker> areaPicker;

    /// The info box: what the base holds about the message on screen, as `i`
    /// asks for it. Absent when it is not up — it is modal, and there is
    /// nothing about it worth remembering between two askings.
    ///
    /// The report itself comes from the base and is read once, when the box
    /// opens: it is a picture of the message as it stood then, and re-reading
    /// it every frame would put the disk behind the scrollbar.
    struct InfoView {
        domain::MessageInfo report;
        /// One row of it as it stands on the screen: the text, and whether it
        /// is a heading over a block rather than one of the block's own lines.
        struct Row {
            std::string text;
            bool heading{false};
        };
        /// The report laid out into rows, and the width it was laid out for.
        /// Rebuilt when the window is resized, the same way the reader re-wraps
        /// a body: how many bytes stand on a row of a hexdump is what the width
        /// decides.
        std::vector<Row> lines;
        int layoutWidth{0};
        int scroll{0};
        /// How many rows the box last showed at once, filled in by its render()
        /// so that the page keys move by what is on the screen rather than by a
        /// second guess at the same arithmetic.
        int rows{1};
    };
    std::optional<InfoView> infoView;

    /// The compiled nodelist, and what stopped it from being opened where it is
    /// empty. Both are filled in the first time Ctrl-N asks for one and never
    /// again: a nodelist is a few megabytes, most sessions never look at one,
    /// and the file is compiled at startup and does not change under us.
    ///
    /// `nodelistOpened` is what says the attempt has been made — an empty
    /// `nodelistDb` with nothing to say about why is a config that names no
    /// nodelist at all, which is not a failure and is what most configs are.
    std::optional<nodelist::NodelistDb> nodelistDb;
    bool nodelistOpened{false};
    std::string nodelistProblem;

    /// The compiled nodelist, opened the first time anything asks for one, or
    /// null where the config names none and where the one it names would not
    /// open — `nodelistProblem` is what says which, and is what the nodelist box
    /// shows along its bottom edge.
    ///
    /// The reading is done here rather than at startup because most sessions
    /// never want it: a nodelist is a few megabytes, and what asks are Ctrl-N and
    /// the sender's location under the header block, neither of which is on the
    /// way to anything.
    [[nodiscard]] const nodelist::NodelistDb* nodelist() {
        if (!nodelistOpened) {
            nodelistOpened = true;
            if (config.nodelistDbPath.empty()) {
                nodelistProblem =
                    _("no nodelist — append nodelist and nodelist_db lines to the "
                      "config");
            } else if (auto opened = nodelist::NodelistDb::open(config.nodelistDbPath)) {
                nodelistDb = std::move(*opened);
            } else {
                nodelistProblem = opened.error()->message();
            }
        }
        return nodelistDb ? &*nodelistDb : nullptr;
    }

    /// The nodelist browser, over the reader: where the cursor is in the
    /// compiled nodelist and what has been typed into its Lookup line. Absent
    /// when it is not up — it is modal, and it opens on the sender of whatever
    /// message is being read, which is the answer it would otherwise be
    /// remembering.
    ///
    /// The nodes are not copied into it: they are read out of the compiled file
    /// as the rows are drawn, a dozen at a time, which is what makes opening it
    /// on a nodelist of forty thousand nodes cost nothing.
    struct NodelistView {
        /// What the box was opened to do, which is what Enter on a row means
        /// and — for the one that looks a name up — what the list holds.
        enum class Purpose {
            /// Ctrl-N in the reader: look something up. Enter walks to the next
            /// place the lookup finds, and nothing is picked.
            Browse,
            /// The compose screen, on a To name with no address under it: the
            /// list is what the name finds, closest first, and Enter fills the
            /// address in.
            PickAddress,
            /// The compose screen, on a To address with no name above it: the
            /// list is the whole nodelist at that address, as Ctrl-N shows it, and
            /// Enter fills the name in.
            PickName,
            /// A `CC:` line naming somebody the nodelist does not answer for
            /// outright — several nodes hold the name, or none does. The list is
            /// what the name found, as `PickAddress` shows it, and Enter says
            /// which of them the copy is for. Closing the box without picking is
            /// an answer too: that copy is not made and the line stays in the
            /// message.
            PickCarbonCopy,
        };
        Purpose purpose{Purpose::Browse};

        /// Which row of the list the cursor is on. In every mode but
        /// `PickAddress` the list is the whole nodelist and a row is a node;
        /// there it is a place in `matches`.
        int cursor{0};
        int offset{0};

        /// The nodes the list is showing, where it is showing what the lookup
        /// found rather than the whole nodelist. `listMatches` is what says
        /// which: a lookup that found nothing is an empty list and not a
        /// nodelist shown whole.
        bool listMatches{false};
        std::vector<size_t> matches;

        /// What has been typed into the Lookup line: an address, whole or in
        /// part, or any part of a sysop's name. Which of the two it is, is
        /// decided by whether it parses as an address — see
        /// `ui/nodelist_dialog.hpp`.
        std::string lookup;
        /// Whether the line still holds, untouched, the address the box opened
        /// on. The first character typed then replaces the whole of it rather
        /// than being added to the end: the address is there to be read and
        /// looked at, and somebody typing over it is asking about something
        /// else — where `2:240/1120` plus a typed `k` is a lookup for nothing at
        /// all. It goes false at the first keystroke that changes the line,
        /// typed or erased, so that everything after that adds to what is there.
        bool seeded{false};

        /// Whether it found anything. The line is drawn in the error color when
        /// it did not, exactly as the area list's quick search is, and the
        /// cursor stays where it was.
        bool found{true};

        /// The box's size, settled when it opens and kept — the same rule the
        /// import and export boxes are built on, and for the same reason: a box
        /// measured against what it is showing would be a different size for
        /// every nodelist, and the row under the pointer would move as the
        /// cursor jumped about. `layoutWidth`/`layoutHeight` are the window it
        /// was measured against, so a window that has been resized is measured
        /// again.
        int inner{0};
        int rows{1};
        int layoutWidth{0};
        int layoutHeight{0};

        /// One row on the screen and its place in the list, filled in by
        /// render() so that a click lands on the row the pointer is over.
        struct Row {
            int index{0};
            term::Box box;
        };
        std::vector<Row> rowBoxes;
    };
    std::optional<NodelistView> nodelistView;
    /// The body wrapped to the current width. The kludge flag travels with each
    /// line because service lines are interleaved with the text, not gathered
    /// into a block.
    struct DisplayLine {
        std::string text;
        bool kludge{false};
        /// Quote nesting of the line this came from, 0 when it is not a quote.
        /// Carried onto wrapped continuations so a long quote keeps one color
        /// even though only its first piece shows the markers.
        int quoteDepth{0};
        /// Part of the tearline/origin block closing the message. Only the block
        /// at the very end counts: `---` in the middle of a message is the
        /// author's own separator and stays ordinary text.
        bool trailer{false};
        /// Where the BBS color codes change the color within this row, in bytes
        /// of `text` — the codes themselves having been taken out of it, since
        /// they are markup and not text. Empty unless `bbs_codes_renegade` is on
        /// and the message actually used one, which is nearly every row.
        ///
        /// Held per row rather than worked out while drawing because a code
        /// colors everything after it, wrapped rows included: a row drawn on its
        /// own — the reader draws only what is on screen — has to know what was
        /// in force where it begins.
        std::vector<bbs::ColorRun> colorRuns;
        /// Where what the reader was told to find stands in this row, in bytes
        /// of `text`. Empty unless a search landed on this message, which is the
        /// only message anything is painted in — see `findHighlight`.
        ///
        /// Held per row for the reason the color runs above are: a long line is
        /// several rows and the reader draws only the ones on screen, so a row
        /// scrolled into from the middle has to carry what is to be lit in it.
        std::vector<encoding::TextMatch> found;
        /// Whether this row is part of an ANSI canvas rather than a line of the
        /// message as it was written. Set only where `bbs_codes_ansi` is on and
        /// the message turned out to hold escape sequences.
        ///
        /// What it decides is what does *not* happen to the row: the style codes
        /// are left alone, because a `*` in a picture is a glyph and not a
        /// marker. The colors are already in `colorRuns` — the canvas hands its
        /// rows over in the same shape the pipe codes leave a line in — so
        /// nothing else about the drawing has to know where a row came from.
        bool canvas{false};
    };
    std::vector<DisplayLine> readLines;
    int readScroll{0};
    /// The window width readLines was laid out for. Not the width the text was
    /// wrapped to: that is two columns narrower when the scrollbar is drawn.
    int readLayoutWidth{0};
    /// Whether the scrollbar is drawn for the current layout. Decided by
    /// relayout(), because it depends on whether the wrapped body overflows.
    bool scrollbarShown{false};

    /// The Find dialog: what to look for and how much of a message to look in.
    /// Absent when it is not up — it is modal, and what it was answered with is
    /// kept in `lastFind` below rather than in the box.
    struct FindPicker {
        /// The words, as UTF-8. It opens holding whatever was last searched
        /// for: looking for the same thing again is the commonest reason to
        /// open it, and the first character typed is against a field the cursor
        /// already stands at the end of.
        std::string query;
        size_t cursor{0};
        /// The byte the field's leftmost column shows, filled in by
        /// `ui::inputField()` — the one place the sideways scroll is worked out,
        /// and so the only thing a click can be measured against.
        size_t origin{0};
        app::SearchScope scope{app::SearchScope::HeaderAndText};

        /// Which stop of the box the cursor is at.
        enum class Focus {
            Query,   ///< the words themselves
            Scope,   ///< the pair of radio buttons under them
            Button,  ///< the Find button closing the box
        };
        Focus focus{Focus::Query};

        /// What the last Enter came to, said in the bottom rule where every
        /// other dialog says it: nothing to look for, or nowhere left in the
        /// area that holds it.
        std::string error;

        /// Where the pieces were drawn, filled in by render() so that a click is
        /// tested against the box rather than against a second copy of its
        /// arithmetic.
        term::Box queryBox;
        term::Box bothBox;
        term::Box headerBox;
        term::Box findBox;

        /// Measured once off the window, as every modal is, and again only when
        /// the window itself changes size.
        int inner{0};
        int layoutWidth{0};
        int layoutHeight{0};
    };
    std::optional<FindPicker> findPicker;

    /// What the last search was and where it came to.
    ///
    /// A search starts on the message in front of the user and runs to the end
    /// of the area. **The same search made again starts on the one after it**,
    /// which is what makes `f`, Enter, `f`, Enter walk from occurrence to
    /// occurrence — and that only holds where the reader is still standing on
    /// what the last search found: a query typed again after walking somewhere
    /// else is a fresh search from where the user now is.
    ///
    /// The area is part of it for the same reason. The same words looked for in
    /// the next area are a first search there, and starting them one message in
    /// would pass over its first message unread.
    struct LastFind {
        std::string query;
        app::SearchScope scope{app::SearchScope::HeaderAndText};
        /// The area it was made in, named the way an area is named everywhere
        /// here — the tag and the path together.
        std::string areaTag;
        std::string areaPath;
        /// The message it landed on, or 0 where nothing has been searched for
        /// yet.
        uint32_t message{0};
    };
    LastFind lastFind;

    /// What is painted in the message on screen, and empty where nothing is.
    ///
    /// It belongs to the one message a search landed on: `loadMessage()` clears
    /// it, so walking to the next message takes the highlight off rather than
    /// carrying it onto a message nobody searched for. It is the query itself
    /// rather than a flag because the rows are laid out again on every resize,
    /// on `k`, and on `b`.
    std::string findHighlight;

    /// Whether a From or To names the user themselves — the name this area is
    /// signed with, which an area group may have a word about.
    ///
    /// The comparison ignores case for ASCII only. A Cyrillic name therefore
    /// has to match exactly, which is the safe way round: names are written the
    /// same way every time in practice, and a miss merely leaves the name in
    /// the ordinary color.
    [[nodiscard]] bool isOwnName(std::string_view name) const {
        return !areaConfig.userName.empty() &&
               config::text::iequals(areaConfig.userName, name);
    }

    /// Whether the message on screen is one the area's `twit` lines cover.
    ///
    /// Read off `areaConfig` rather than off `config`, since a group may state
    /// the twits: a name worth ignoring in one echo is a name like any other in
    /// the next. `show` is asked first because it is the setting that says
    /// "there are no twits here", and then nothing has to be matched at all.
    [[nodiscard]] bool isTwit(const domain::MessageHeader& header) const {
        return areaConfig.twitMode != config::TwitMode::Show && areaConfig.isTwit(header);
    }

    /// Whether the message is addressed to the user themselves: the name this
    /// area is signed with, or — where the area addresses anybody at all — one
    /// of our own addresses.
    ///
    /// What `twit_mode skip` spares. A twit writing *to* you is the one message
    /// of theirs you did ask to see, if only to know what was said.
    [[nodiscard]] bool addressedToUser(const domain::MessageHeader& header) const {
        if (isOwnName(header.to)) return true;
        return header.destAddr.isValid() && areaConfig.isOwnAddress(header.destAddr);
    }

    /// Whether the reader walks past the message rather than opening it —
    /// `skip` and `ignore`, which differ in nothing else.
    [[nodiscard]] bool twitSkipped(const domain::MessageHeader& header) const {
        if (!isTwit(header)) return false;
        switch (areaConfig.twitMode) {
            case config::TwitMode::Ignore: return true;
            case config::TwitMode::Skip: return !addressedToUser(header);
            default: return false;
        }
    }

    /// Whether the text of the message on screen stands hidden behind the
    /// notice.
    ///
    /// `blank` hides every twit, which is what it is for. `skip` and `ignore`
    /// hide the ones they would have walked past — a message is only in front
    /// of the user at all when there was nowhere to walk to, and an area whose
    /// every message is a twit is then read exactly as `blank` reads it. What
    /// `skip` *spares* is not hidden: a twit writing to you is a message you
    /// asked to see, and being made to press a key for it would be answering
    /// the wrong question. `kill` hides them too, for the area whose base would
    /// not be written: the messages the sweep could not take out should not
    /// come back as ordinary mail.
    ///
    /// The header block is no part of it either way: whose message is being
    /// passed over is exactly what the user is entitled to see.
    [[nodiscard]] bool twitHidden() const {
        if (!readHeader || twitRevealed) return false;
        switch (areaConfig.twitMode) {
            case config::TwitMode::Blank:
            case config::TwitMode::Kill: return isTwit(*readHeader);
            case config::TwitMode::Skip:
            case config::TwitMode::Ignore: return twitSkipped(*readHeader);
            case config::TwitMode::Show: return false;
        }
        return false;  // unreachable; a TwitMode is one of the five
    }

    /// Whether the twit on screen has been asked for after all — what Space
    /// does where the notice stands. It goes back off with the message: the
    /// next one is somebody else's, and having read one is no reason to be
    /// shown the next unasked.
    bool twitRevealed{false};

    /// Whether the scrollbar is shown beside the message body. `b` toggles it;
    /// the config decides where it starts.
    bool showScrollbar{true};

    /// Whether links in the message text are underlined, from the config.
    bool underlineLinks{true};

    /// Whether the kludges are on screen. They are off by default: they are
    /// service data, interesting when something looks wrong and noise otherwise.
    bool showKludges{false};

    // --- composing --------------------------------------------------------
    /// The header of the message being written, filled in by the rules in
    /// app/compose_prefill.hpp and edited on the compose screen.
    app::ComposeFields compose;
    /// Where a message picked out of the area dialog is to go — a reply moved
    /// into another area, or a message forwarded into one. Only meaningful
    /// while `compose.moved` or `compose.forward` holds; what is left in it
    /// afterwards is the last area picked, which nothing reads.
    domain::AreaConfig targetArea;

    /// The message being changed: its number in the area on screen, and what
    /// the base holds about it that the editor does not show — the control
    /// lines standing either side of the text, and the charset it is written
    /// in. Both are only meaningful while `compose.changing` holds; storing the
    /// message is what puts them back around what the editor has made of it.
    uint32_t changeNumber{0};
    app::PreservedLines changeKept;

    /// The `CC:` and `XC:` lines of the message being written, from the moment
    /// storing it found them to the moment it is stored. Absent the rest of the
    /// time, which is what says the question has not been asked yet: the
    /// editor asks it once per attempt to store, and `process` is the answer.
    ///
    /// It is a run rather than a result because carrying the commands out can
    /// stop halfway: a `CC:` naming somebody several nodes answer to puts the
    /// nodelist up, and the walk goes on from `command`/`token` when the box has
    /// been answered. Everything it has decided so far is here, so that nothing
    /// has to be worked out twice — least of all the recipients, whom the user
    /// has been picking one by one.
    struct CopyRun {
        /// Whether the commands are commands. Ignore leaves them as text, and
        /// the message is stored with the lines exactly as they were typed.
        bool process{true};
        std::vector<app::CopyCommand> commands;
        /// How far the walk has got: which command, and which token of it.
        size_t command{0};
        size_t token{0};

        std::vector<app::CarbonCopy> carbons;
        std::vector<app::Crosspost> crossposts;
        /// Which command lines are to stay in the message: the ones whose
        /// recipients or areas nobody could find, and — where the config asks
        /// for `keep`/`raw` — every one of them. A line is never dropped on
        /// behalf of something that did not happen.
        std::vector<size_t> keep;
        /// What was written and could not be resolved, in the words the box
        /// afterwards says it in.
        std::vector<std::string> unresolved;
        /// Whether a mask covered the area the message is being written in and
        /// was written with a `#`, which is what leaves "Originally in" unsaid.
        bool currentHidden{false};

        /// The message once the lists are in it, and the `CC:` kludges the
        /// hidden form asks for. Both are filled in when the walk ends, and are
        /// what the message and every copy of it are built from.
        std::vector<std::string> text;
        std::vector<std::string> kludges;
    };
    std::optional<CopyRun> copyRun;

    /// The area the message being written belongs to: the one being read, or —
    /// when the dialog has named one — the area picked there. It is what the
    /// compose screens show in their title and what the message is built
    /// against, the area deciding the sender's AKA and whether there is a
    /// recipient to address.
    [[nodiscard]] const domain::AreaConfig& composeArea() const {
        return compose.moved || compose.forward ? targetArea : currentArea;
    }

    /// Whether what is being written goes somewhere other than the area on
    /// screen. It is the one thing that makes storing it anything more than a
    /// write to the base already open — a forward into the area being read is
    /// an ordinary new message, whatever it was begun by.
    [[nodiscard]] bool composeGoesElsewhere() const {
        return composeArea().tag != currentArea.tag ||
               composeArea().path != currentArea.path;
    }

    /// The config as it stands in the area the message being written goes into,
    /// which is not always the one on screen: a reply moved into another area,
    /// or a message forwarded there, is written under that area's settings and
    /// not under the ones it was read under.
    ///
    /// Worked out again when that area changes and not otherwise, and owned here
    /// rather than made where it is wanted, so that a BuildRequest may keep a
    /// reference to it.
    [[nodiscard]] const config::AppConfig& composeConfig() {
        if (composeConfigTag_ != composeArea().tag) {
            composeConfig_ = config.effectiveFor(composeArea());
            composeConfigTag_ = composeArea().tag;
        }
        return composeConfig_;
    }

    /// Enters an area: the one being read, and the settings it is read under.
    /// The two go together — an `areaConfig` resolved for one area while another
    /// is on screen is the whole of what could go wrong here — so nothing else
    /// assigns to `currentArea`.
    void setCurrentArea(const domain::AreaConfig& area) {
        currentArea = area;
        areaConfig = config.effectiveFor(area);
    }

    /// Which field of it the cursor is in, as a compose::Field, and where in
    /// that field's text it sits — a byte offset, always on a character
    /// boundary.
    int composeField{0};
    size_t composeCursor{0};

    /// Whether the cursor is in the header block rather than in the text. The
    /// compose screen is one screen: the fields stand above the message from
    /// the moment it is begun, and this says which of the two the typing goes
    /// into. A new message opens in the header — there is a recipient to name —
    /// and a reply opens in the text, its header having been filled in from the
    /// message it answers.
    bool composeInHeader{false};

    /// Where the Change button beside the attributes was drawn, filled in by the
    /// compose screen's render().
    term::Box changeAttributesBox;

    /// Where the compose screen drew something the cursor can be put into, and
    /// which byte of that text the leftmost column shows.
    ///
    /// The origin is not always zero: a field narrower than what has been typed
    /// into it is drawn scrolled sideways, and then a column counted from the
    /// left edge says nothing about which character stands there.
    struct ComposeSpot {
        term::Box box{term::Box::Nowhere()};
        size_t origin{0};
    };
    /// One per header field, in compose::Field order, filled in by the compose
    /// screen's render() so that a click lands on the field the pointer is over
    /// rather than on a second guess at the same arithmetic. The To address is
    /// Nowhere in echomail, whose row does not carry one.
    std::vector<ComposeSpot> composeFieldSpots;
    /// One per row of the message text, top to bottom — the row on screen and
    /// not the line in the buffer, which is that plus `editScroll`. Only the row
    /// the cursor is on ever has an origin: it is the only one drawn scrolled.
    std::vector<ComposeSpot> composeTextRows;

    /// Where the editor drew the delete-line button, row by row, filled in by
    /// its render(). Three boxes rather than one: the button is drawn a cell at
    /// a time, laid out inside the rows of the text, and there is nothing
    /// standing over all three rows for a single box to be reflected out of. Its
    /// top and bottom are left `Nowhere()` where the cursor is on the first or
    /// the last row of the message and neither is drawn.
    struct DeleteLineSpots {
        term::Box top{term::Box::Nowhere()};
        term::Box label{term::Box::Nowhere()};
        term::Box bottom{term::Box::Nowhere()};

        /// Whether the cell is on the button — anywhere in the box, as a click
        /// on the back button is anywhere in its two rows: what is being pointed
        /// at is one thing, whichever row of it the pointer landed on.
        [[nodiscard]] bool contains(int x, int y) const {
            return top.Contain(x, y) || label.Contain(x, y) || bottom.Contain(x, y);
        }
    };
    DeleteLineSpots composeDeleteLine;

    /// The attributes dialog: the attributes of the message being written, while it
    /// is being edited over the header screen. Absent when it is not up — it is
    /// modal, and there is nothing about it worth remembering between two
    /// askings.
    ///
    /// The attributes themselves are not in here: every toggle lands on
    /// `compose.attributes` as it is made, so the row under the addresses shows
    /// what the dialog is doing while it is doing it.
    struct AttributePicker {
        /// Which attribute the cursor is on, as a place in the dialog's own table.
        int cursor{0};
        /// What the message carried when the dialog opened, so that Esc can put
        /// it back.
        uint32_t before{0};
        /// Where each checkbox was drawn, and where the Done button was, filled
        /// in by the dialog's render() so that a click is tested against the
        /// frame rather than against a second copy of its arithmetic.
        std::vector<term::Box> boxes;
        term::Box doneBox;
    };
    std::optional<AttributePicker> attributePicker;

    /// The import dialog: which file is being read into the message, and how.
    /// Absent when it is not up — it is modal, and what is worth keeping
    /// between two askings is kept beside it rather than in it, on the three
    /// fields under this one.
    struct ImportPicker {
        std::vector<DirEntry> entries;
        int cursor{0};
        int offset{0};
        /// What has been typed of a name so far, as the area dialog's own quick
        /// search works. Empty means no search is up.
        std::string search;

        /// The path box under the title: the directory being shown, until it is
        /// typed over. Enter on it walks into a directory, reads a file, or says
        /// the path is not there — see `ui/import_dialog.hpp`.
        ///
        /// It is put back to the directory whenever the listing changes, so what
        /// it says is what is on the screen unless the user is in the middle of
        /// saying otherwise.
        std::string path;
        size_t pathCursor{0};
        /// The byte its leftmost column shows: a path outgrows the box easily,
        /// and a column counted from the left edge says nothing about which
        /// character stands there. Filled in by the field as it is drawn.
        size_t pathOrigin{0};

        /// Which of the dialog's stops the typing is on.
        enum class Focus {
            Path,
            Files,
            Mode,
        };
        Focus focus{Focus::Files};

        /// The box's size, settled when it opens and kept: the list is as many
        /// rows as the window had room for then, and the frame as wide, whatever
        /// the directory walked into since holds. A box measured against its
        /// contents jumps about as they change — the modal would stand a
        /// different size in every directory, and an error line at the bottom
        /// would push the whole of it up a row.
        ///
        /// `layoutWidth`/`layoutHeight` are the window it was measured against.
        /// A window that has been resized is measured again, exactly as the info
        /// box lays its report out again: what must not move is the box under a
        /// user walking a listing, not the box under a window being dragged.
        int inner{0};
        int rows{0};
        int layoutWidth{0};
        int layoutHeight{0};

        /// What the last attempt could not do, in the words the user is to
        /// read, or empty. It is shown inside the box: the dialog is still up
        /// and is the place to say so, where a modal error over it would be the
        /// loudest thing the interface can do about a path that was mistyped.
        std::string error;
        /// What the file came to, once one has been read. The dialog does the
        /// reading, since it is what has to say when it fails; putting the lines
        /// into the message is the editor's.
        std::vector<std::string> lines;

        /// Where each row and each control was drawn, filled in by the dialog's
        /// render() so that a click is tested against the frame rather than
        /// against a second copy of its arithmetic.
        struct Row {
            int index{0};
            term::Box box;
        };
        std::vector<Row> rowBoxes;
        term::Box pathBox{term::Box::Nowhere()};
        term::Box textModeBox{term::Box::Nowhere()};
        term::Box uueModeBox{term::Box::Nowhere()};
    };
    std::optional<ImportPicker> importPicker;

    /// What the last import settled on: the directory it was in and the mode it
    /// went in as. Kept out here rather than in the picker because they are
    /// exactly what is worth remembering between two askings — a file picker
    /// that opened in the home directory every time would be asking again for
    /// an answer already given. The directory is empty until the dialog has been
    /// opened once, which is when the working directory fills it in.
    ///
    /// The charset a text file is read in is not among them: it is the locale's,
    /// which is what a file on this machine is written in and not something to
    /// be asked about.
    std::string importDirectory;
    app::ImportMode importMode{app::ImportMode::Text};

    /// The export dialog: where the message on screen is to be written, and
    /// under what name. Absent when it is not up — it is modal, and what is
    /// worth keeping between two askings is kept on the two fields under it.
    ///
    /// It is the import dialog's shape with the answers the other way round: the
    /// listing shows **directories only**, since what is being picked there is
    /// somewhere to write, and the row under it is the name to write under
    /// rather than the mode to read in.
    ///
    /// The mode is the import's two the other way round as well, and it is not
    /// asked for here: a message carrying uuencoded files is what raises the
    /// question, and `ExportModePicker` below is where it is answered.
    struct ExportPicker {
        /// What the export writes. Text is the message itself, in the charset
        /// the locale names; Uue is the files the message carries, decoded out
        /// of it and written under the names it gave them.
        enum class Mode {
            Text,
            Uue,
        };
        Mode mode{Mode::Text};
        /// The files, decoded when the question was asked. Empty in text mode,
        /// and never empty in the other — the question is not asked where the
        /// message carries nothing.
        std::vector<app::UueFile> files;

        std::vector<DirEntry> entries;
        int cursor{0};
        int offset{0};
        /// What has been typed of a name so far, the quick search of the list.
        std::string search;

        /// The path box under the title, exactly as the import dialog's: the
        /// directory being shown until it is typed over, and Enter on it walks
        /// into what it names or says the path is not there. A file typed there
        /// is the name to write under rather than something to open — see
        /// `ui/export_dialog.hpp`.
        std::string path;
        size_t pathCursor{0};
        size_t pathOrigin{0};

        /// The name the message is written under, and where the cursor sits in
        /// it. Seeded from the message itself the first time and kept between
        /// askings on `AppState::exportName`, so that a second message can be
        /// added to the same file without typing it again.
        size_t nameCursor{0};
        size_t nameOrigin{0};

        /// Which of the dialog's stops the typing is on. `Name` is the box the
        /// name is typed into in text mode and the **Save button** in the other:
        /// the names of the files came out of the message, so they are a label
        /// the ring walks past, and the button under them is what takes Enter.
        enum class Focus {
            Path,
            Files,
            Name,
        };
        Focus focus{Focus::Name};

        /// The box's size, settled when it opens and kept — see the note on the
        /// import picker's, which this is in every way.
        int inner{0};
        int rows{0};
        int layoutWidth{0};
        int layoutHeight{0};

        /// What the last attempt could not do, in the words the user is to read,
        /// written along the bottom frame. Empty when nothing went wrong.
        std::string error;

        /// The question a file already standing under the name raises: written
        /// over, added to, or neither. Absent while it is not up.
        ///
        /// It stands **over the export box rather than in its place**, and the
        /// box behind it is what says which name is being asked about — the
        /// question is the last step of the write it interrupted, not a screen
        /// of its own, and Esc puts it away and leaves the export dialog exactly
        /// as it was, with the name there to be typed over.
        ///
        /// Only text mode ever raises it. A decoded file is written over
        /// nothing at all: those names came out of the message and there is
        /// nowhere here to change one, so there is no answer to offer.
        struct Existing {
            /// The two answers, in the order they are drawn and stepped
            /// through. The destructive one is not what the box opens on.
            enum class Answer {
                Overwrite,
                Append,
            };
            Answer answer{Answer::Append};
            /// What was found there, settled when the question went up so that
            /// answering it writes to the file that was asked about.
            std::string path;
            /// Where the two answers were drawn, filled in by the dialog's
            /// render() so that a click is tested against the frame rather than
            /// against a second copy of its arithmetic.
            term::Box overwriteBox;
            term::Box appendBox;
        };
        std::optional<Existing> existing;

        /// Where each row and each control was drawn, filled in by the dialog's
        /// render() so that a click is tested against the frame rather than
        /// against a second copy of its arithmetic.
        struct Row {
            int index{0};
            term::Box box;
        };
        std::vector<Row> rowBoxes;
        term::Box pathBox{term::Box::Nowhere()};
        term::Box nameBox{term::Box::Nowhere()};
    };
    std::optional<ExportPicker> exportPicker;

    /// The question the export asks first where the message carries uuencoded
    /// files: are they to be taken out of it, or is the message to be written as
    /// the text it also is? Absent when it is not up — it is modal, and the
    /// export dialog follows it in its place, so the two boxes read as one
    /// question in two halves the way `m`'s do.
    ///
    /// The files are decoded before the box goes up rather than after it is
    /// answered: whether there is anything in the message to ask about is
    /// exactly what the decoding answers, and decoding it twice would be reading
    /// the same message twice to be told the same thing.
    struct ExportModePicker {
        ExportPicker::Mode mode{ExportPicker::Mode::Uue};
        std::vector<app::UueFile> files;
        /// Where the two answers were drawn, filled in by the dialog's render()
        /// so that a click is tested against the frame rather than against a
        /// second copy of its arithmetic.
        term::Box filesBox;
        term::Box textBox;
    };
    std::optional<ExportModePicker> exportModePicker;

    /// Where the last export went and what it was called. Kept for the same
    /// reason the import directory is: a dialog that asked again for an answer
    /// already given is a dialog that has not been paying attention — and a name
    /// used once is where the message after it usually belongs, an export
    /// appending to what is already there.
    ///
    /// **The name is the user's and nothing else.** Nothing invents one: the box
    /// is empty until it is typed into, and Enter on an empty one says so rather
    /// than writing a file nobody named. A name made up from the message would
    /// be right for the message it was made from and quietly wrong for every one
    /// after it, and a file written under it is not something the user can be
    /// shown having asked for.
    std::string exportDirectory;
    std::string exportName;

    /// The text of the message being written, and the first line of it on
    /// screen. Filled in from the template when the editor opens.
    TextBuffer edit;
    int editScroll{0};
    /// That text exactly as the template produced it, kept so that a header
    /// still being filled in can be answered by expanding the template again.
    ///
    /// The text is written before the header is, now that the two stand on one
    /// screen, and a template greets the recipient by name and closes the
    /// message with an origin carrying the sender's address — both of them
    /// fields the user has yet to type. Whenever the header is left for the
    /// text, `edit.lines` is compared against this: unchanged means nothing has
    /// been written over the template's words, so they are expanded again
    /// against the header as it now stands. A single keystroke in the text
    /// makes the message the user's, and it is never rewritten after that.
    std::vector<std::string> composeStartText;
    /// The header those words were chosen against, field by field. A trip up
    /// into the block and back down that changed nothing leaves the text as it
    /// stands rather than building the same lines again — which would put the
    /// cursor back at the template's @position, throwing away wherever in the
    /// message the user had got to.
    std::vector<std::string> composeStartHeader;

    // --- modal confirmation ------------------------------------------------
    /// What the confirmation dialog is asking about, None when none is up. It
    /// is modal: while one is up, keys go to the dialog rather than to the
    /// screen underneath.
    enum class Confirm {
        None,
        Quit,           ///< leaving the area list, and so AmberEdit
        SaveMessage,    ///< storing what the editor holds
        DropMessage,    ///< leaving the editor without storing it
        DeleteMessage,  ///< taking the message in the reader out of the base
        /// Changing a message that is not the user's own — the one case where
        /// the notice at the head of it is asked for as well.
        ChangeForeignMessage,
        /// Changing one of the user's own that has already gone out: what the
        /// network has seen of it cannot be changed with it.
        ChangeSentMessage,
        /// The message being stored carries `CC:` or `XC:` lines. The two
        /// answers are Process and Ignore rather than Yes and No — the message
        /// is stored either way, and what is being asked is whether the
        /// commands in it are commands or are text.
        ProcessCopies,
    };
    Confirm confirm{Confirm::None};
    /// The answers a confirmation offers. Two, whatever is being asked: a
    /// question with a third way out of it is a question that was not the one
    /// to ask.
    enum class ConfirmChoice {
        Yes,
        No,
    };
    /// Which button it has selected. Yes, so that Esc followed by Enter goes
    /// through without a detour; the dialog is there to catch a key pressed by
    /// mistake, and that is answered by Esc again or by n.
    ConfirmChoice confirmChoice{ConfirmChoice::Yes};
    /// Where the buttons were last drawn, filled in by the dialog's own
    /// render(). The box is centred on the screen, so a click is tested against
    /// what the layout actually produced rather than against a second copy of
    /// the same arithmetic.
    term::Box confirmYesBox;
    term::Box confirmNoBox;

    // --- modal error --------------------------------------------------------
    /// What went wrong, in the words the user is to read, or empty when no such
    /// box is up. It is modal in the same way the confirmation is: while it
    /// stands there is nothing underneath for a key to mean.
    ///
    /// The interface has nowhere to say anything once it is up — there is no
    /// status line — so a screen that cannot do what was asked normally just
    /// does not do it, having said so by drawing the button for it in the menu
    /// dimmed. This is for the one thing that has no such warning in
    /// front of it: an area that will not open, asked for by a key that until
    /// then had every reason to work.
    std::string errorMessage;
    /// Where acknowledging it leaves the user. The area list, for the box this
    /// was written for: it stands in place of a screen that would not open, so
    /// the stack under it has nothing worth coming back to. False for a box
    /// that only reports something about a screen still standing — the copies a
    /// `CC:` line named nobody for — where the user was in the middle of
    /// something and is left there.
    bool errorEndsScreen{true};
    /// Where its one button was last drawn, filled in by the dialog's own
    /// render(): the box is centred, so a click is tested against the layout
    /// rather than against a second copy of its arithmetic.
    term::Box errorOkBox;

    // --- click feedback -----------------------------------------------------
    /// Which button a click has just landed on. It is drawn inverted for as
    /// long as the click animation lasts and None the rest of the time, so
    /// that a press is seen before the screen it leads to replaces this one.
    ///
    /// Only one at a time, and only for those few milliseconds: this says what
    /// the pointer is doing right now, not what the interface holds.
    enum class Pressed {
        None,
        Back,        ///< the Back button in the top-left corner
        ConfirmYes,  ///< the confirmation dialog's answers
        ConfirmNo,
        ErrorOk,           ///< the one button of the error box
        ForwardChoice,     ///< one of Forward/Move/Copy, which `pressedLink` says
        ExportChoice,      ///< one of the export's two, which `pressedLink` says
        ExistingChoice,    ///< Overwrite or Append, where a file was already there
        ExportSave,        ///< the button that writes the files a message carries
        FindScope,         ///< one of the find dialog's two, which `pressedLink` says
        FindButton,        ///< the button that runs the search
        ThreadLink,        ///< one of the thread markers beside the message number
        UrlLink,           ///< a link in the message text, by its place in the frame
        MenuButton,        ///< the menu button in the top-right corner
        DeleteLine,        ///< the editor's delete-line button, beside the cursor
        Menu,              ///< one of the buttons in the menu it opens
        ChangeAttributes,  ///< the button opening the attributes dialog, on the header
        AttributesDone,    ///< the button closing it again
        Hint,              ///< one of the hints along the bottom, by its index
    };
    Pressed pressed{Pressed::None};
    /// Which one of them, where there are several of a kind: the message a
    /// thread marker names — they are drawn from the message's own thread and
    /// there is no index that would outlive a frame — or the menu button's
    /// place in the column. Zero for the buttons there is only one of.
    uint32_t pressedLink{0};

    /// How long a click is shown for before it acts, in milliseconds, from the
    /// config. Zero turns the animation off.
    int clickAnimationMs{100};

    // --- the wheel ----------------------------------------------------------
    /// What the wheel has done lately, for the lists whose rows can stand more
    /// than one line tall. One counter for the wheel rather than one per list:
    /// there is one wheel, and a run of notches is a run of them wherever the
    /// pointer is; how tall a row of the list under it stands is handed in a
    /// notch at a time.
    WheelThrottle listWheel;

    /// What time it is, in milliseconds off a monotonic clock — the only clock
    /// the interface reads, and it reads it for one thing: how far apart two
    /// notches of the wheel arrived. It is a member so that a test can hand the
    /// screens a clock it moves itself, a real wheel being the one thing a test
    /// cannot roll.
    std::function<Millis()> monotonicMs{[] {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
            .count();
    }};

    /// How far one notch of the wheel moves the cursor of a list whose rows
    /// stand `rowHeight` lines tall: the notch itself, or nothing where it is
    /// one of the notches `list_wheel_throttle` spends on the row already moved
    /// onto. A swallowed notch is still the wheel being turned — the screen
    /// answers it by doing nothing rather than by leaving it to whatever is
    /// underneath.
    [[nodiscard]] int wheelSteps(int delta, int rowHeight) {
        if (!config.listWheelThrottle) return delta;
        return listWheel.step(delta, rowHeight, monotonicMs(),
                              config.listWheelThrottleMs);
    }

    /// Draws the interface as it stands, at once. Filled in by the shell, which
    /// is what owns the terminal; the tests leave it unset. It is for the two
    /// places that have to put something on the screen from inside a call
    /// rather than at the top of the loop — the click animation below, and the
    /// rescan naming the area it is on.
    std::function<void()> drawFrame;

    /// Draws it and leaves it there for the length of the click animation. The
    /// pause is the whole difference from drawFrame, and it lives in the shell
    /// with the terminal rather than in this header.
    std::function<void()> holdFrame;

    /// Draws the interface as it stands, if there is anything to draw it with.
    /// The tests run with neither callback set and simply skip the frame.
    void redraw() const {
        if (drawFrame) drawFrame();
    }

    /// Shows a click before acting on it.
    ///
    /// With a button named, that button is drawn inverted. With none — which is
    /// what the lists ask for, once the cursor has been moved onto the row that
    /// was clicked — the state is simply held as it stands, and the row the
    /// pointer landed on is on screen as the current one before it opens.
    ///
    /// The pause is spent inside here, so what follows the call is what the
    /// user asked for, written as if there were no animation at all.
    void showClick(Pressed what = Pressed::None, uint32_t link = 0) {
        if (!holdFrame || clickAnimationMs <= 0) return;
        pressed = what;
        pressedLink = link;
        holdFrame();
        pressed = Pressed::None;
        pressedLink = 0;
    }

    /// Whether `what` is the button being pressed — what the screens ask while
    /// drawing, so that none of them has to spell the comparison out. `which`
    /// tells one of several apart and is zero for the rest, which is what
    /// nothing pressed leaves behind.
    [[nodiscard]] bool isPressed(Pressed what, uint32_t which = 0) const {
        return pressed == what && pressedLink == which;
    }

    AppState(app::AreaManager& areaManager, const config::AppConfig& appConfig)
        : manager(areaManager),
          config(appConfig),
          // Both are seeded rather than left blank: before any area is entered
          // there is no tag to work a group out for, and the file's own settings
          // are the answer for that state as much as for an area no group
          // covers.
          areaConfig(appConfig),
          showScrollbar(appConfig.showScrollbar),
          underlineLinks(appConfig.underlineLinks),
          clickAnimationMs(appConfig.clickAnimationMs),
          composeConfig_(appConfig) {}

    /// Lines available for the message list: the screen minus the title, the
    /// column headings and the separator.
    ///
    /// Lines, not messages: a row may stand more than one line tall, and
    /// `messageListItems()` is how many messages that leaves room for.
    [[nodiscard]] int messageListRows() const {
        constexpr int kChrome = 3;
        return height <= kChrome ? 1 : height - kChrome;
    }

    /// What a row of the message list holds in this window: `msglist_format`'s
    /// two formats read on the same line as everything else adaptive, and on
    /// every frame for the same reason the area list's are.
    [[nodiscard]] const config::MsgListFormat& messageListFormat() const {
        return wideWindow() ? config.messageListFormatWide
                            : config.messageListFormatNarrow;
    }

    /// How many lines tall one row of the message list stands: a line per line
    /// of the format this window follows, and never less than one.
    [[nodiscard]] int msgRowHeight() const {
        return std::max(1, static_cast<int>(messageListFormat().size()));
    }

    /// How many messages the list shows at once — the lines it has, divided by
    /// how tall a row is. The lines a row and a half would have stood in are
    /// left blank: half a message says nothing, and a row cut across the bottom
    /// of the screen would be read as one that is all there.
    [[nodiscard]] int messageListItems() const {
        return std::max(1, messageListRows() / msgRowHeight());
    }

    /// What the message beside the reader's sidebar is never left less than.
    /// The panel is a width the config states outright, and a config may state
    /// one the window has no room for: what would be left is not a message but a
    /// strip, so in that window the panel is simply not up.
    static constexpr int kSidebarMinPane = 24;

    /// The columns the reader's sidebar stands in, from `reader_sidebar_width`.
    /// The rule closing it off is a column of its own and no part of this.
    ///
    /// A fixed strip rather than a share of the window: what the panel holds is
    /// two names, a stamp and a subject, and a column of them that re-shared
    /// itself every time the terminal was dragged would shuffle its fields about
    /// under a reader who only wanted a wider message. The window grows into the
    /// message instead, which is what the window is for.
    [[nodiscard]] int readerSidebarWidth() const { return config.readerSidebarWidth; }

    /// Whether the reader has that panel up: `reader_sidebar_threshold` columns
    /// or more, and enough left over beside it for a message to be read in.
    /// `off` is the threshold at zero, and no window is ever wide enough.
    ///
    /// Asked on every frame, like every other width the interface answers to.
    [[nodiscard]] bool readerSidebarShown() const {
        if (config.readerSidebarThreshold <= 0) return false;
        if (width < config.readerSidebarThreshold) return false;
        return width - readerSidebarWidth() - 1 >= kSidebarMinPane;
    }

    /// The columns the reader itself lays out in — the whole window, less the
    /// panel and the rule beside it where one is up. Everything the reader
    /// draws is measured against this rather than against `width`: the title,
    /// the header block, the rules, the body's wrapping and its scrollbar.
    [[nodiscard]] int readerPaneWidth() const {
        return readerSidebarShown() ? width - readerSidebarWidth() - 1 : width;
    }

    /// Which column the reader itself begins in, which is what a click on
    /// something in its top-left corner is measured from.
    [[nodiscard]] int readerPaneLeft() const {
        return readerSidebarShown() ? readerSidebarWidth() + 1 : 0;
    }

    /// How many lines tall one row of the sidebar stands: a line per line of
    /// `reader_sidebar_msglist_format`, and never less than one.
    [[nodiscard]] int readerSidebarRowHeight() const {
        return std::max(1, static_cast<int>(config.readerSidebarFormat.size()));
    }

    /// How many messages the sidebar shows at once. It carries no title and no
    /// column headings — the reader's own title says which area and which
    /// message of how many — so it runs the whole height of the screen.
    [[nodiscard]] int readerSidebarItems() const {
        return std::max(1, height / readerSidebarRowHeight());
    }

    /// Lines available for the area list, which carries no title — the area
    /// names are the screen's own heading. The chrome is that heading row and
    /// the rule under it.
    ///
    /// Lines, not areas: a row may stand more than one line tall, and
    /// `areaListItems()` is how many areas that leaves room for.
    [[nodiscard]] int areaListRows() const {
        constexpr int kChrome = 2;
        return height <= kChrome ? 1 : height - kChrome;
    }

    /// Whether the window counts as a wide one.
    ///
    /// `adaptive_ui_threshold` is where the interface goes from laying things out
    /// side by side to stacking them — eighty columns unless the config says
    /// otherwise: it is the width a terminal has had since there were
    /// terminals, and the width FTN messages are written to, so a window
    /// narrower than that is one the user has deliberately made small.
    [[nodiscard]] bool wideWindow() const { return width >= config.adaptiveUiThreshold; }

    /// What a row of the area list holds in this window: `arealist_format`'s
    /// two formats read on the same line as everything else adaptive, and on
    /// every frame for the same reason — a window can be dragged, and the row
    /// follows it.
    [[nodiscard]] const config::AreaListFormat& areaListFormat() const {
        return wideWindow() ? config.areaListFormatWide : config.areaListFormatNarrow;
    }

    /// How many lines tall one row of the area list stands: a line per line of
    /// the format this window follows, and never less than one.
    [[nodiscard]] int areaRowHeight() const {
        return std::max(1, static_cast<int>(areaListFormat().size()));
    }

    /// How many areas the list shows at once — the lines it has, divided by how
    /// tall a row is. The lines a row and a half would have stood in are left
    /// blank: half an area says nothing, and a row cut across the bottom of the
    /// screen would be read as one that is all there.
    [[nodiscard]] int areaListItems() const {
        return std::max(1, areaListRows() / areaRowHeight());
    }

    /// Whether something the config gives an on/off/when_narrow/when_wide to is
    /// on the screen: what it says, and under the two window-led values which
    /// side of `adaptive_ui_threshold` the window is on — `when_narrow` under it,
    /// `when_wide` at it and over.
    ///
    /// Asked on every frame rather than settled once, for the same reason the
    /// reader's header layout is: the window is what answers `when_narrow` and
    /// `when_wide`, and a window can be dragged.
    [[nodiscard]] bool shown(config::Visibility visibility) const {
        switch (visibility) {
            case config::Visibility::On: return true;
            case config::Visibility::Off: return false;
            case config::Visibility::WhenNarrow: return !wideWindow();
            case config::Visibility::WhenWide: return wideWindow();
        }
        return false;  // unreachable; a Visibility is one of the four
    }

    /// Whether a screen shows the menu button in its top-right corner. A menu
    /// with no commands in it is no menu either: the corner would open a box
    /// with nothing in it to press.
    [[nodiscard]] bool menuButtonShown(const std::vector<Command>& commands) const {
        return !commands.empty() && shown(config.menuButton);
    }

    [[nodiscard]] bool readerMenuShown() const {
        return menuButtonShown(config.readerMenu);
    }
    [[nodiscard]] bool composeMenuShown() const {
        return menuButtonShown(config.composeMenu);
    }

    /// Where each hint of the bottom row landed, and what it runs — filled in
    /// by `hint_bar::render()` so that a click is answered against what was
    /// drawn rather than against a second guess at the same arithmetic. In the
    /// order they are drawn, which is the order `Pressed::Hint` numbers them in.
    struct HintSpot {
        Command command{};
        term::Box box;
    };
    std::vector<HintSpot> hintSpots;

    /// Whether the last row of the terminal is the hint bar, from `hint_bar`.
    ///
    /// Asked once a frame in `runApp()`, which takes the row off `height`
    /// before a screen lays itself out in it — so every screen is one row
    /// shorter with the bar up, and none of them has to know why.
    [[nodiscard]] bool hintBarShown() const { return shown(config.hintBar); }

    /// Whether the screens draw the Back button over their top-left corner.
    /// Read from the config on every frame rather than copied once:
    /// `when_narrow` and `when_wide` are answered by a window that can be
    /// dragged, and Esc goes back either way, so a corner that comes and goes
    /// takes nothing with it.
    [[nodiscard]] bool backButtonShown() const { return shown(config.backButton); }

    /// Whether the editor draws the delete-line button down its two rightmost
    /// columns, from `compose_delete_line_button`. Asked on every frame like
    /// every other `Visibility`, and asked before the text is laid out as well
    /// as before it is drawn: the three columns come off the width the lines are
    /// broken at, so this is what the soft wrap is decided against.
    [[nodiscard]] bool composeDeleteLineShown() const {
        return shown(config.composeDeleteLineButton);
    }

    /// Rows the header block stands on whatever the config says, in the reader
    /// and in the editor alike: From, To, Subj and the Date row under them.
    static constexpr int kHeaderRows = 4;

    /// Whether the header block carries the Recd row under the Date row, from
    /// `show_recd_date` — off unless it is asked for. Asked on every frame like
    /// every other `Visibility`: `when_wide` and `when_narrow` are answered by a
    /// window that can be dragged.
    [[nodiscard]] bool recdRowShown() const { return shown(config.showRecdDate); }

    /// Rows the header block actually stands on: the four it always has, and the
    /// Recd row where the config asks for one. Both screens count their chrome
    /// from this, so the block and the text under it move together.
    ///
    /// The row is there whether or not the message on the screen ever arrived
    /// from anywhere: a block that grew and shrank from one message to the next
    /// would walk the body up and down the window while reading through an area.
    [[nodiscard]] int headerRows() const {
        return kHeaderRows + (recdRowShown() ? 1 : 0);
    }

    /// Height of the message body viewport. The chrome is the title, a rule,
    /// the rows of the header table and a second rule.
    [[nodiscard]] int readRows() const {
        const int chrome = 3 + headerRows();
        return height <= chrome ? 1 : height - chrome;
    }

private:
    /// What composeConfig() last worked out, and the tag it worked it out for.
    /// Private where everything else here is public: they are the memo and not
    /// the answer, and reading them without the accessor would read a config
    /// resolved for whichever area was being written into before this one.
    config::AppConfig composeConfig_;
    std::string composeConfigTag_;
};

}  // namespace amberedit::ui
