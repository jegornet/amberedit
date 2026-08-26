#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "config/area_pattern.hpp"
#include "config/cfg_file.hpp"
#include "config/commands.hpp"
#include "config/list_format.hpp"
#include "domain/address_pattern.hpp"
#include "domain/area.hpp"
#include "domain/ftn_address.hpp"
#include "domain/message.hpp"

namespace amberedit::config {

/// One `akamatch` entry: an AKA of ours and the destinations it is used for.
/// An empty pattern list is meaningful — the AKA exists (an `aka` line names
/// one) and can be picked by hand, it is simply never chosen on its own.
struct AkaMatch {
    domain::FtnAddress aka;
    std::vector<domain::AddressPattern> patterns;
};

/// One `address_macro` line: a short word typed where a netmail recipient goes,
/// and the whole recipient it stands for.
///
/// It is what makes writing to a robot a thing one does from memory —
/// `af` for the AreaFix at one's uplink, password and `K/s` and all — rather
/// than a row of an address, a password and an attribute dialog to be got right
/// each time.
///
/// The subject and the attributes are optional and separately so: a macro may
/// name an address alone, or an address and a subject, or an address and the
/// attributes to write it with. Nullopt means the line said nothing about it,
/// and what the message already carries is left as it is.
struct AddressMacro {
    /// The word typed into the To name field. Matched without regard to case,
    /// and whole: a macro is what the field holds, not something found inside a
    /// name that happens to contain it.
    std::string macro;
    /// Who the message is addressed to — the name that goes into the field the
    /// macro was typed into.
    std::string name;
    domain::FtnAddress address;
    std::optional<std::string> subject;
    /// The FTS-0001 bits, from the short forms `domain::messageAttributes()`
    /// writes. They are *added* to what the message carries rather than put in
    /// its place: `Loc` and `Pvt` are what a netmail of one's own starts with,
    /// and a macro naming `K/s` is asking for one attribute and not for the loss
    /// of those two.
    std::optional<uint32_t> attributes;
};

/// What is left in the message where its `CC:` lines stood, from
/// `compose_cc_list`.
///
/// The message goes out to somebody who cannot see the copies being made, so
/// what the list is for is telling them who else has this message. Five
/// answers, from "the line as it was typed" to "nothing at all", with the
/// hidden one apart from the rest: it says the same thing to a program reading
/// the message and nothing to a person reading it.
enum class CarbonList {
    Keep,     ///< the `CC:` lines stay exactly as they were typed
    Names,    ///< one line, the names on it separated by commas and wrapped
    Visible,  ///< a line per recipient, the address beside the name
    Hidden,   ///< a `CC:` kludge per recipient, at the head of the message
    Remove,   ///< the lines go and nothing stands in their place
};

/// The same for the `XC:`/`XP:` lines, from `compose_xc_list`. Four answers
/// rather than five: an echo a message was crossposted to is nothing to hide
/// from the people reading it, so there is no hidden form.
enum class CrosspostList {
    Raw,      ///< the `XC:` lines stay exactly as they were typed
    Verbose,  ///< one line, the echotags on it separated by commas and wrapped
    Yes,      ///< a line per echo
    None,     ///< the lines go and nothing stands in their place
};

/// What the reader does with a message the `twit` lines cover, from
/// `twit_mode`.
///
/// Five answers to one question — how much of somebody one does not want to
/// read is worth having on the screen — running from "all of it, as though the
/// lines had never been written" to "none of it, ever again".
enum class TwitMode {
    Show,    ///< nothing at all: the lines are written and the messages stand
    Blank,   ///< the text replaced by a notice, Space showing it after all
    Skip,    ///< walked past, unless it is addressed to the user themselves
    Ignore,  ///< walked past whatever it says
    Kill,    ///< deleted from the base, unasked, as the area is opened
};

/// One `twit` line: whom the messages that are not to be read are from — or to,
/// where `twit_to` is on.
///
/// A line names either an FTN address pattern or a name, and which of the two
/// it is, is settled by whether it parses as an address holding a zone: nothing
/// else about a name could be mistaken for one, and a name is what everything
/// that is not an address is. Both take wildcards, and they are not the same
/// wildcards — an address's `*` stands for a component and a name's for any run
/// of characters — because they are patterns over two different things.
struct TwitRule {
    /// The name glob, `text::globMatches` matching it whole. Empty exactly when
    /// the line named an address.
    std::string name;
    /// The address pattern, where the line named one.
    std::optional<domain::AddressPattern> address;

    /// Whether the rule covers this end of a message: the name written there
    /// and the address beside it, either of which may be all the message
    /// carries.
    [[nodiscard]] bool matches(std::string_view who,
                               const domain::FtnAddress& addr) const;
};

/// What one letter of `arealist_sort` sorts the area list by.
enum class AreaSortKey {
    Address,  ///< 'a' — the AKA the area is presented under
    Echoid,   ///< 'e' — the area tag
    Group,    ///< 'g' — the tosser's group, where the format has one
    Type,     ///< 't' — netmail first, then echo, then the local kinds
    Unread,   ///< 'u' — how many messages are unread
};

/// One criterion of `arealist_sort`: what to sort by and which way round.
struct AreaSortCriterion {
    AreaSortKey key{AreaSortKey::Echoid};
    /// `-` before the letter. Ascending is the default, as `+` says out loud.
    bool descending{false};

    friend bool operator==(const AreaSortCriterion& a, const AreaSortCriterion& b) {
        return a.key == b.key && a.descending == b.descending;
    }
};

/// What one field of `arealist_format` shows.
enum class AreaFieldKind {
    Number,       ///< 'a' — the area's place in the list, counted from one
    Echoid,       ///< 'e' — the area tag
    Description,  ///< 'd' — the tosser config's -d, or what an echolist says
    Group,        ///< 'g' — the tosser's group, where the format has one
    Total,        ///< 'c' — how many messages the area holds
    Unread,       ///< 'u' — how many of them stand after the lastread mark
    UnreadFlag,   ///< 'n' — a star against an area with anything unread
    Space,        ///< ' ' — a gap between two fields, and the only way to a gap
};

/// One field of `arealist_format`: what it shows and how wide it stands.
struct AreaListField {
    AreaFieldKind kind{AreaFieldKind::Echoid};
    /// Columns. Zero is what `e0` asks for: the field takes what the
    /// fixed-width ones leave on the line it stands on, and where several on
    /// one line ask, they share it equally.
    int width{0};

    friend bool operator==(const AreaListField& a, const AreaListField& b) {
        return a.kind == b.kind && a.width == b.width;
    }
};

/// One line of a row of the area list: the fields on it, in the order written.
///
/// A line is what a width of `0` is measured against — the fields under it are
/// laid out among that line's own columns, and a fixed-width field on one line
/// says nothing about what the flexible ones on the next have to share.
using AreaListLine = std::vector<AreaListField>;

/// What a row of the area list holds: the lines it is drawn on, in order. A
/// format written without `\n` in it is the one line every row used to be, so
/// the front line is always there to read.
using AreaListFormat = std::vector<AreaListLine>;

/// What one field of `msglist_format` shows.
enum class MsgFieldKind {
    Number,   ///< 'a' — the message's number in the area, counted from one
    From,     ///< 'f' — who the message is from
    To,       ///< 't' — who it is to
    Subject,  ///< 's' — what it is about
    Date,     ///< 'd' — when it was written, in the column's own format
    Space,    ///< ' ' — a gap between two fields, and the only way to a gap
};

/// One field of `msglist_format`: what it shows, how wide it stands, and — for
/// the Date column — what its stamp is written with.
struct MsgListField {
    MsgFieldKind kind{MsgFieldKind::Subject};
    /// Columns. Zero is what `s0` asks for: the field takes what the rest of
    /// its line leaves, and where several on one line ask, they share it
    /// equally. `kAutoWidth` is what the number and the date stand at where the
    /// format names no width for them — a width worked out from what the field
    /// holds rather than one written down.
    int width{0};
    /// The strftime format a Date column writes its stamp with, from the
    /// brackets after the letter — `d(%d %b %y)`. Empty means
    /// `reader_datetime_format`, and it stays empty rather than being filled in
    /// here: the config is read a line at a time and `msglist_format` may stand
    /// above `reader_datetime_format` in the file, so the two are only ever put
    /// together where the column is drawn. Nothing but a Date column has one.
    std::string dateFormat;

    friend bool operator==(const MsgListField& a, const MsgListField& b) {
        return a.kind == b.kind && a.width == b.width && a.dateFormat == b.dateFormat;
    }
};

/// One line of a row of the message list: the fields on it, in the order
/// written — the same thing `AreaListLine` is for the other list.
using MsgListLine = std::vector<MsgListField>;

/// What a row of the message list holds: the lines it is drawn on, in order.
using MsgListFormat = std::vector<MsgListLine>;

/// Which description an area is shown by where the tosser config and an echolist
/// both have one, from `arealist_description_priority`.
enum class DescriptionPriority {
    Area,      ///< `area` — what the tosser config or an `area` block says
    Echolist,  ///< `echolist` — what the echolists say
};

/// One `echolist` line: the file it names, and the charset that file is written
/// in.
struct EcholistSource {
    /// The path, with a leading `~/` expanded. A `.zip` is an archive of
    /// echolists and anything else is one echolist.
    std::string path;
    /// The charset the line stated, and empty where it stated none — which is
    /// the locale's, worked out when the file is read rather than here. Stored
    /// as the nothing it was, so that the compiled file records what the config
    /// said and not what the machine happened to answer.
    std::string charset;

    friend bool operator==(const EcholistSource& a, const EcholistSource& b) {
        return a.path == b.path && a.charset == b.charset;
    }
};

/// Whether a piece of the interface is on the screen at all: the menu button,
/// from `menu_button`, the back button, from `back_button`, the editor's
/// delete-line button, from `compose_delete_line_button`, and the header
/// block's Recd row, from `show_recd_date`.
///
/// One enumeration for all of them because the question is the same one, and so
/// is the answer the two window-led values give: the window's width against
/// `adaptive_ui_threshold`, measured on every frame. `menu_button` is kept apart
/// from the lists of commands beside it so that the corner can be taken away and
/// put back without the commands in the menu having to be written down again.
///
/// `when_narrow` and `when_wide` are the same line read from its two sides, and
/// both are spelled out rather than one being "adaptive": which way round an
/// adaptive setting went was the thing nobody could remember, and a value
/// naming the window it wants leaves nothing to remember.
enum class Visibility {
    On,          ///< always, whatever the window is
    Off,         ///< never, and what it took goes back to what the screen shows
    WhenNarrow,  ///< only in a window narrower than `adaptive_ui_threshold`
    WhenWide,    ///< only in a window of `adaptive_ui_threshold` columns or more
};

/// Where the hints stand in the last row of the screen, from `hint_bar_align`:
/// the rest of the row is the rule that closes the interface, and this is which
/// side of the hints it runs along.
enum class HintAlign {
    Left,    ///< against the left edge, the rule filling what is left
    Center,  ///< in the middle, with rule on both sides
    Right,   ///< against the right edge, the rule in front of them
};

/// Which side of the message the reader's sidebar stands on, from
/// `reader_sidebar_position`. The rule closing the panel off goes with it, so
/// this is the whole of the difference between the two.
enum class SidebarPosition {
    Left,   ///< between the window's edge and the message
    Right,  ///< past the message, against the window's other edge
};

/// Format of the tosser config the area list comes from.
/// It is stated explicitly in the config: guessing it from the file contents
/// is one more source of surprises, and users know their own tosser.
enum class TosserConfigFormat {
    Fidoconfig,  ///< hpt-style: "EchoArea <tag> <path> -b squish ..."
    AreasBbs,    ///< line-based areas.bbs
    SquishCfg,   ///< Squish's own squish.cfg, with its attached -$ options
};

/// One `group ... endgroup` block of AmberEdit's own config: the areas it covers
/// and the settings it gives them.
///
/// This is not `domain::AreaConfig::group`, which is the *tosser's* group letter
/// — a different thing with the same name, and the one place the two meet is a
/// reader confusing them. An area group is AmberEdit's own and is matched on the
/// echotag; the tosser's group is a label it prints in a column.
///
/// The settings are kept as the lines they were written on rather than as a
/// field each, and resolving a group is the same code that reads a line at the
/// top level running over them again. That is what makes a new setting one edit
/// — a branch in the chain and a word in the table of what a group may say —
/// instead of a field, a parse and an apply that can drift apart. Every group is
/// applied once while the config is read, so a line that cannot be read stops
/// AmberEdit at startup and not at the area it covers.
struct AreaGroup {
    std::vector<AreaTagPattern> members;
    std::vector<CfgEntry> settings;
    /// Where `group` stood, for the message an ambiguous pair of them throws.
    int line{0};

    /// Whether the group states this setting at all — a different question from
    /// what it resolves to, and the one AreaManager asks before letting a
    /// group's address win over the tosser's.
    [[nodiscard]] bool states(std::string_view key) const;

    /// How much of the tag the group's best-matching member pins down, or
    /// nullopt when none of them covers the area at all.
    [[nodiscard]] std::optional<std::tuple<int, int, bool>> specificityFor(
        std::string_view tag) const;
};

/// One `area ... endarea` block: an area declared by AmberEdit's own config
/// rather than read out of a tosser's, and the line the block was opened on.
///
/// The line is kept because the one thing that can go wrong with such a block is
/// not visible while the config is read: a tag the tosser also declares. The
/// tosser config is opened later, by the area source, and the complaint it makes
/// there has to be able to name the line the tag was written on.
struct ManualArea {
    domain::AreaConfig area;
    int line{0};
};

/// AmberEdit's own config. The area list mostly is not duplicated here — it
/// comes from the tosser config, and what this file adds to it are the
/// `area ... endarea` blocks, for the bases no tosser knows about.
struct AppConfig {
    /// Path to the tosser config (areas / areas.bbs). Empty where the config
    /// names none, which only an `area ... endarea` block makes possible: the
    /// area list has to come from somewhere.
    std::string tosserConfigPath;
    /// Format of that config. Required as soon as there is a path — there is
    /// deliberately no default.
    TosserConfigFormat tosserConfigFormat{TosserConfigFormat::Fidoconfig};

    /// The nodelists to compile, from the `nodelist` lines and in the order they
    /// were written — which is their order of precedence, the first one to name
    /// an address being the one that keeps it.
    ///
    /// A line names a file, or the pattern one of two kinds of nodelist arrives
    /// under: `Z2DAILY.999` for the newest of the files whose extension is a day
    /// number, and `Z2PNT.Z99` for the newest of the zip archives whose
    /// extension is `Z` and two digits, the nodelist being the file inside it.
    /// Which one a line is, is settled by `nodelist::NodelistSpec`; nothing here
    /// looks at the disk.
    std::vector<std::string> nodelistSources;

    /// Where the compiled nodelist is — the file the `nodelist` lines are
    /// compiled into and read back from, from `nodelist_db`. Required as soon as
    /// there is a `nodelist` line, since compiling has to put the result
    /// somewhere; on its own it is a config that reads a nodelist somebody else
    /// compiles.
    std::string nodelistDbPath;

    /// The echolists to compile, from the `echolist` lines and in the order they
    /// were written — which is their order of precedence, the first one to name
    /// an echo being the one that keeps it.
    ///
    /// A line names a file, or a zip archive holding several: an archive is
    /// unpacked without paths and every `.lst` and `.na` in it is read. The
    /// optional second value is the charset that file is written in, and a line
    /// that states none is read in the locale's. Nothing here looks at the disk.
    std::vector<EcholistSource> echolistSources;

    /// Where the compiled echolist is — the file the `echolist` lines are
    /// compiled into and read back from, from `echolist_db`. Required as soon as
    /// there is an `echolist` line, since compiling has to put the result
    /// somewhere; on its own it is a config that reads an echolist somebody else
    /// compiles.
    std::string echolistDbPath;

    /// Which description an area is shown by where it has two, from
    /// `arealist_description_priority`. `Area` by default — what the tosser
    /// config or an `area ... endarea` block says about an echo is what the
    /// person running this system decided, and an echolist is what the rest of
    /// the network agreed.
    ///
    /// Only a description that says something counts either way: whichever is
    /// preferred, an empty one steps aside for the other.
    DescriptionPriority areaDescriptionPriority{DescriptionPriority::Area};

    /// What the area list's description column shows for an area no description
    /// was found for, from `arealist_description_default`. "no description" by
    /// default: a blank column reads as a row that has not been drawn yet,
    /// where the words say the description is missing rather than the row.
    ///
    /// Empty is a setting of its own — `arealist_description_default ""` leaves
    /// the column blank, as it was before there was anything to say here. Only
    /// the column is concerned: `@CDESC` and the rest speak for the message
    /// being written, and "no description" in a message would be a line the
    /// program wrote in the user's name.
    ///
    /// The one default here that is a word and not a number or a path, and so
    /// the one a translation replaces: `fromEntries()` sets it from the catalog,
    /// which is not loaded yet where this initializer runs.
    std::string areaDescriptionDefault{"no description"};

    /// The keyboard layout to read, from `keys`. Empty where the config names
    /// none, which is the ordinary case and means AmberEdit's own layout.
    ///
    /// A file that is named is the layout **entire**: a command it does not
    /// mention has no key at all. Reading it is `ui::KeyMap`'s — a layout is
    /// about keystrokes and screens, neither of which this layer knows anything
    /// about — and all this holds is where it is.
    std::string keysPath;

    /// A directory for work that needs one, from `tmpdir`. Today that is
    /// unpacking a zipped nodelist or echolist; it is not a nodelist setting as
    /// such, which is why it is not named after one.
    ///
    /// Empty where the config states none, and that is not a failure: this is
    /// what `config::makeTempDir` is handed, and it falls back on the system's
    /// own temporary directory — a thing only the machine being run on can
    /// answer for, and so no thing for a config parser to answer. The setting is
    /// for naming somewhere else.
    std::string tempDirPath;

    /// A file to write the errors the interface swallows to, from `error_log`.
    /// Empty where the config names none, which is the ordinary case and means
    /// nothing is written down anywhere.
    ///
    /// It is for the two places that have nowhere to say anything: a frame that
    /// would not draw and a keystroke that threw, both caught in
    /// `ui/app_shell.cpp` so that one broken area cannot take the application
    /// down. Writing it is `ui::error_log`'s — this layer only holds where the
    /// file is.
    std::string errorLogPath;

    /// Character set a message being *read* is decoded from when it carries no
    /// CHRS kludge — or one that names no particular encoding, "IBMPC" being
    /// the name that does that. Nothing else can say: no tosser config format
    /// has a per-area charset option. Required.
    std::string defaultCharset;

    /// Character set a message being *written* is encoded in, and that its own
    /// CHRS kludge announces. Separate from `defaultCharset` because the two
    /// answer different questions: what the echoes one reads are written in is
    /// not necessarily what one wants to write in. Required.
    std::string composeCharset;

    /// What the `CC:` and `XC:`/`XP:` commands leave behind in the text of a
    /// message that carried them, from `compose_cc_list` and `compose_xc_list`.
    ///
    /// Two settings rather than one because they are two lists: a carbon copy
    /// is a person and a crosspost is an echo, and somebody who wants the
    /// echoes named at the foot of the message need not want the recipients
    /// named as well. Both are stated as the list the reader of the message
    /// sees, which is what the commands are turned into: `keep`/`raw` is the
    /// one value that leaves the command line itself standing.
    ///
    /// An area group may state either, so an echo where the copies are worth
    /// naming can say so on its own.
    CarbonList carbonList{CarbonList::Names};
    CrosspostList crosspostList{CrosspostList::Verbose};

    /// Which lastread slot in a message base belongs to this user.
    ///
    /// Squish's .sql file and the Fido *.msg `lastread` file are plain arrays
    /// indexed by this number, so on a single-user system the default of zero
    /// is the right answer and matches what other readers use. JAM finds its
    /// record by the CRC of the user's name instead and keeps this number in
    /// the record's UserID field. Whatever another reader on the same base uses
    /// (GoldED's FIDOUSERNO / SQUISHUSERNO) is what belongs here.
    int lastreadUser{0};

    /// How the area list is ordered, from `arealist_sort`: the criteria in the
    /// order they were written, the first one deciding and the rest breaking
    /// its ties. Areas the whole list leaves equal stay in the order the tosser
    /// config names them, which is the only order the user themselves wrote.
    ///
    /// The default groups the netmail areas at the top and puts each kind in
    /// alphabetical order under it — `arealist_sort te`. An empty list means the
    /// tosser config's own order, which `arealist_sort ""` asks for.
    std::vector<AreaSortCriterion> areaListSort{{AreaSortKey::Type, false},
                                                {AreaSortKey::Echoid, false}};

    /// What each row of the area list holds in a window narrower than
    /// `adaptive_ui_threshold`, from `arealist_format`'s first value: the lines
    /// the row is drawn on, and on each the fields in the order they were
    /// written, each as wide as it was asked for.
    ///
    /// The default is `"e c u\nd n"` — the area's name across whatever the two
    /// counts leave, and under it the description across whatever the star
    /// leaves. A narrow window has no room to put the description beside the
    /// name, and a line of its own is the room it does have. The group is in
    /// neither default: areas.bbs has no groups at all, and where a fidoconfig
    /// has them `g` puts the column back.
    AreaListFormat areaListFormatNarrow{{{AreaFieldKind::Echoid, 0},
                                         {AreaFieldKind::Space, 1},
                                         {AreaFieldKind::Total, 4},
                                         {AreaFieldKind::Space, 1},
                                         {AreaFieldKind::Unread, 4}},
                                        {{AreaFieldKind::Description, 0},
                                         {AreaFieldKind::Space, 1},
                                         {AreaFieldKind::UnreadFlag, 1}}};

    /// What each row holds in a window of `adaptive_ui_threshold` columns or
    /// more, from `arealist_format`'s second value — a line written with one
    /// format gives that format to both, so a config that says nothing about
    /// wide windows keeps behaving as it did.
    ///
    /// The default is `"e d c un"`, one line: a wide window has the columns to
    /// put the description beside the name, so a row there is a row and the
    /// screen holds twice the areas the narrow one does.
    AreaListFormat areaListFormatWide{{{AreaFieldKind::Echoid, 0},
                                       {AreaFieldKind::Space, 1},
                                       {AreaFieldKind::Description, 0},
                                       {AreaFieldKind::Space, 1},
                                       {AreaFieldKind::Total, 4},
                                       {AreaFieldKind::Space, 1},
                                       {AreaFieldKind::Unread, 4},
                                       {AreaFieldKind::UnreadFlag, 1}}};

    /// What each row of the message list holds in a window narrower than
    /// `adaptive_ui_threshold`, from `msglist_format`'s first value — read the
    /// same way `arealist_format` is, and standing in the same relation to the
    /// wide window's format below.
    ///
    /// The default is `"a f0 t0 d(%d %b %y)\ns"`: the number, the two names
    /// sharing whatever the number and the stamp leave of the first line, and
    /// the subject across the whole of the second. A narrow window has no
    /// columns to put the subject beside the names, and a line of its own is the
    /// room it does have.
    ///
    /// The stamp is the day and nothing else, and written out here rather than
    /// left to `reader_datetime_format`: a narrow row has three columns to keep
    /// steady beside it, and a format with no `%z` in it measures the same from
    /// one screenful to the next, where the reader's own would widen and narrow
    /// as the zones in the messages on screen came and went. The column is
    /// measured rather than pinned because there is now nothing to pin it
    /// against.
    MsgListFormat messageListFormatNarrow{{{MsgFieldKind::Number, kAutoWidth},
                                           {MsgFieldKind::Space, 1},
                                           {MsgFieldKind::From, 0},
                                           {MsgFieldKind::Space, 1},
                                           {MsgFieldKind::To, 0},
                                           {MsgFieldKind::Space, 1},
                                           {MsgFieldKind::Date, kAutoWidth, "%d %b %y"}},
                                          {{MsgFieldKind::Subject, 0}}};

    /// What each row of the message list holds in a window of
    /// `adaptive_ui_threshold` columns or more, from `msglist_format`'s second
    /// value. One format on the line gives both, as it does for the area list.
    ///
    /// The default is `"a f t s d(%d %b %y %H:%M)"`, one line: the names stand
    /// at their own twenty columns, the stamp takes what it needs, and the
    /// subject has the rest — which is the table the list has always drawn, said
    /// in the letters the setting is written in. A wide window has room for the
    /// minute the narrow one leaves off, and no more use for the zone than it
    /// has: a column of stamps is read down for which day a message is from.
    MsgListFormat messageListFormatWide{
        {{MsgFieldKind::Number, kAutoWidth},
         {MsgFieldKind::Space, 1},
         {MsgFieldKind::From, 20},
         {MsgFieldKind::Space, 1},
         {MsgFieldKind::To, 20},
         {MsgFieldKind::Space, 1},
         {MsgFieldKind::Subject, 0},
         {MsgFieldKind::Space, 1},
         {MsgFieldKind::Date, kAutoWidth, "%d %b %y %H:%M"}}};

    /// Whether the area list draws the reader's scrollbar beside its rows, from
    /// `arealist_scrollbar`.
    ///
    /// Off by default, the rows keeping the whole width. Switched on it costs
    /// them a column only where there is something to scroll: a list that fits
    /// on one screen draws no bar, exactly as the reader shows none for a
    /// message that fits.
    bool areaListScrollbar{false};

    /// Whether the message list draws the reader's scrollbar beside its rows,
    /// from `msglist_scrollbar`. On by default, and drawn on the same terms as
    /// `areaListScrollbar` where that one is switched on.
    bool messageListScrollbar{true};

    /// The width, in columns, at which the reader puts the list of messages up
    /// its left-hand side, from `reader_sidebar_threshold`.
    ///
    /// Zero is no panel at all, however wide the window is dragged, and it is a
    /// width rather than a flag: no window is nought columns wide, so the width
    /// that never comes is the one way of saying never that keeps this a single
    /// question. The config may write it as `0` or as `off`, which are the same
    /// answer read two ways.
    ///
    /// **Zero unless the config asks for a panel.** The reader is a screen for
    /// reading one message on, and a panel is the screen given over to something
    /// else — worth having where somebody wants it and not worth appearing
    /// unasked the first time a terminal is dragged wide. A hundred and twenty
    /// is the width to turn it on at: `reader_sidebar_width` and the rule beside
    /// it come to forty, so a window that size is the narrowest that still
    /// leaves the message the eighty columns an FTN message is written to.
    ///
    /// Its own threshold rather than `adaptive_ui_threshold`: that line is where
    /// the interface stops laying things side by side, and this is a whole panel
    /// rather than a column, so it wants a window wider than a merely wide one.
    ///
    /// Read on every frame, like every other width the interface answers to: a
    /// window can be dragged, and the panel comes and goes with it.
    int readerSidebarThreshold{0};

    /// The columns that panel stands in, from `reader_sidebar_width` — the rule
    /// closing it off is a column of its own and no part of it.
    ///
    /// Thirty-nine by default, which is what leaves the message its eighty
    /// columns in a window of exactly `reader_sidebar_threshold`: eighty is what
    /// an FTN message is written to, and the window where the panel first
    /// appears should not also be the one where the art in a message starts
    /// wrapping. A wider window gives the whole of the difference to the
    /// message; the panel is a fixed strip, so the messages in it do not shuffle
    /// between their columns every time the terminal is dragged.
    ///
    /// A width leaving too little beside it is not clamped — the panel simply
    /// stays off in a window that narrow, which is `readerSidebarShown()`.
    int readerSidebarWidth{39};

    /// Which side of the message that panel stands on, from
    /// `reader_sidebar_position`.
    ///
    /// The right by default. The message is what the screen is for, and a
    /// message that begins in the window's first column begins where the eye
    /// already is: the panel is the thing glanced at, so it goes where a thing
    /// glanced at goes. `left` is for whoever reads it as the message list it
    /// came out of and wants it standing where a list stands.
    ///
    /// Nothing else about the panel changes with it — the threshold, the width
    /// and the format are one setting on either side, and the rule closing it
    /// off moves with the panel.
    SidebarPosition readerSidebarPosition{SidebarPosition::Right};

    /// What each row of that panel holds, from `reader_sidebar_msglist_format`
    /// — `msglist_format`'s letters, read by the same parser, and one format
    /// rather than two: the panel is only ever on the screen in a window wide
    /// enough for it, so there is no narrow window to write a second for.
    ///
    /// The default is `"f0 t0 d(%d %b %H:%M)\ns"`: the two names sharing what
    /// the stamp leaves of the first line, and the subject across the whole of
    /// the second. No number column — which message of how many is in the
    /// reader's own title, a column of digits down a panel this narrow costs
    /// the names more than it says.
    MsgListFormat readerSidebarFormat{{{MsgFieldKind::From, 0},
                                       {MsgFieldKind::Space, 1},
                                       {MsgFieldKind::To, 0},
                                       {MsgFieldKind::Space, 1},
                                       {MsgFieldKind::Date, kAutoWidth, "%d %b %H:%M"}},
                                      {{MsgFieldKind::Subject, 0}}};

    /// Whether the message list paints a message nobody has read yet in
    /// `msglist_unread`, from `highlight_unread`.
    ///
    /// On by default, and the message list is the whole of it: the reader shows
    /// the message itself and says nothing about whether it had been read
    /// before it was opened, which by then it has been. Off, the list draws as
    /// it always did — the marks are still made in the base, since they are the
    /// message's and other readers go by them.
    bool highlightUnread{true};

    /// Whether the reader starts with the scrollbar shown. `b` toggles it for
    /// the session; this only decides where it starts.
    bool showScrollbar{true};

    /// Whether links in the message text are underlined as well as colored.
    /// On by default: a theme may put the link color close to the text's, and
    /// the underline says "address" whatever the colors do.
    bool underlineLinks{true};

    /// The program a click on a link in the message text runs, from
    /// `urlhandler`: the command and its arguments as they were written, with
    /// `$url` still standing wherever it was — the link is put in its place at
    /// the moment the program is run, and a command naming it nowhere is
    /// refused as the config is read.
    ///
    /// Empty by default, which is what makes a click on a link do nothing:
    /// there is no browser every machine has, and guessing at one would open
    /// something the user never named.
    std::vector<std::string> urlHandler;

    /// What `urlhandler` writes the link in place of, wherever in an argument
    /// it stands. Spelled here rather than where the program is run: the config
    /// is what refuses a command that names it nowhere, and the two have to
    /// agree on the word.
    static constexpr std::string_view kUrlPlaceholder = "$url";

    /// Whether the reader acts on the style codes in a message: `_underlined_`,
    /// `*bold*`, `/italic/`, `#inverted#`. Off by default — the markers are
    /// punctuation as often as they are markup, and a message that never meant
    /// them as markup should read as it was written.
    bool styleCodes{false};

    /// **Experimental.** Whether the reader acts on the BBS color codes — pipe
    /// codes — a message may be written with, in the Renegade/Telegard dialect:
    /// `|00` to `|31`, a foreground, a background, and nothing else about the
    /// text. Off by default, and per area, since an echo either is written that
    /// way or is not: where it is not, `|` is a character like any other, and a
    /// reader taking three bytes out of every line that happens to hold one
    /// would be losing text.
    bool bbsCodesRenegade{false};

    /// **Experimental.** Whether the reader replays the ANSI graphics a message
    /// may have been written with — the escape sequences a BBS drew a picture
    /// on the caller's terminal with. Off by default, and per area.
    ///
    /// The option says an echo *may* carry ANSI; each message is still looked at
    /// on its own, and only one that actually holds an escape sequence is drawn
    /// as a canvas. See `ui/ansi_canvas`, and note that a canvas suspends
    /// `styleCodes` and `bbsCodesRenegade` above however they are set: inside a
    /// picture they would be reading glyphs somebody drew with as markup.
    bool bbsCodesAnsi{false};

    /// Whether the reader leaves the area at its ends: → on the last message
    /// and ← on the first go back to the area list, from `reader_edge_exit`.
    ///
    /// On by default. A key that does nothing at all says nothing about why,
    /// and reading an area to its end is exactly the moment the next area is
    /// wanted. Off, the two keys stop at the ends as they always did.
    bool edgeExit{true};

    /// Where entering an area from the area list lands, from
    /// `reader_lastread_auto_next`: on the message after the lastread mark, or
    /// on the marked one itself.
    ///
    /// On by default. The mark names the message last read, so the first thing
    /// wanted in that area is the one after it — every FTN reader opens an area
    /// on the first unread message for that reason. Off, the area opens on the
    /// marked message, which is the last one actually read.
    ///
    /// Neither setting reaches past the end of the area: a mark on the newest
    /// message has nothing after it, and the reader opens on that message
    /// either way. Neither reaches before its front either — an area with no
    /// mark has had nothing read in it, so it opens on its first message
    /// whichever way this stands: there is no marked message to open on, and no
    /// message before the first one to open after.
    bool lastreadAutoNext{true};

    /// Whether an answer follows the `AREA:` line of the message it answers,
    /// from `areareplydirect`.
    ///
    /// A message that arrived in a packet carries the echo it was posted to as
    /// an `AREA:` line — no ^A in front of it, and standing before MSGID and
    /// every other kludge, which is the only place the line means anything. A
    /// base that keeps it therefore holds messages that say where they came
    /// from, and answering one in the area it names is what the writer of it
    /// would expect; the area it is being read in may be a dupe collector, a
    /// bad-message area or a carbon copy, where an answer would be read by
    /// nobody.
    ///
    /// On by default, and off is the plain behaviour: the answer stays in the
    /// area on screen, as every other reply does. Either way this only decides
    /// where `q`/F4 and the Reply button aim — `n` asks which area, and an
    /// answer it is given is the user's own word on the matter.
    ///
    /// An area group may state it, so a base whose `AREA:` lines are worth
    /// following can say so without every other area following them.
    bool areaReplyDirect{true};

    /// Where the answers to an area's messages belong, from `reply_to_area`:
    /// the tag of the area `n` puts its cursor on before the user has moved it,
    /// and — where the area named is a netmail one — the area the `CC:` copies
    /// of a message written in an echo are put into.
    ///
    /// The two are one statement: an echo whose answers are written by netmail
    /// says so once, and both the dialog and the carbon copies follow it. An
    /// area group may state it, which is how one echo says it and the rest do
    /// not.
    ///
    /// Empty unless the config says otherwise, and then the dialog opens at the
    /// top of the list. A tag no area in the list carries is the same as none:
    /// the areas come from the tosser config, which this setting cannot add to,
    /// and a dialog refusing to open over a stale name would be worse than one
    /// opening where it always did.
    ///
    /// Only where the cursor *starts*. Nothing is picked by it, and the search
    /// and the arrows move off it as they always have.
    std::string replyToArea;

    /// The `twit` lines, in the order they were written: whom this config would
    /// rather not read. Empty unless the config says otherwise — there is
    /// nobody AmberEdit would think of ignoring on its own.
    ///
    /// A group's lines are *added* to these rather than put in their place: the
    /// people one does not read anywhere are not un-ignored by an echo having a
    /// name of its own to add, and a group cannot take a twit off — which is
    /// what `twit_mode show` is for, being the one thing that says "not here".
    std::vector<TwitRule> twits;

    /// The `twit_subj` lines: the subjects that are not to be read, whoever
    /// wrote them. Globs like a `twit` name, and added by a group the same way.
    std::vector<std::string> twitSubjects;

    /// Whether a `twit` line covers the messages written *to* that name or
    /// address as well as the ones written from it, from `twit_to`.
    ///
    /// On by default: a thread with somebody one does not read is mostly not
    /// worth reading either — the answers quote the whole of what was said.
    /// Off leaves the lines about senders alone, which is what an echo where
    /// the answers are worth more than the message is asking for.
    ///
    /// It says nothing about `twit_subj`: a subject is the message's own and
    /// has no direction to have an opinion about.
    bool twitTo{true};

    /// What is done with a message the lines cover, from `twit_mode`. `blank`
    /// unless the config says otherwise: it is the one that loses nothing —
    /// the message is on the screen, said to be a twit, and one keystroke away
    /// from being read after all.
    TwitMode twitMode{TwitMode::Blank};

    /// Whether the message is one the twits cover: its sender, its recipient
    /// where `twit_to` is on, or its subject. Any one of the lines is enough —
    /// they are a list of things not worth reading and not a description of one
    /// thing.
    ///
    /// Asked of the config resolved for the area the message is being read in,
    /// which is what `AppState::areaConfig` holds: a name worth ignoring in one
    /// echo is a name like any other in the next.
    [[nodiscard]] bool isTwit(const domain::MessageHeader& header) const;

    /// The width, in columns, at which a window stops counting as a narrow one,
    /// from `adaptive_ui_threshold` — the single line `when_narrow` and
    /// `when_wide` are read against: under it a window is narrow, at it and
    /// over it is wide.
    ///
    /// Eighty by default, which is the width a terminal has had since there were
    /// terminals and the width FTN messages are written to, so a window narrower
    /// than that is one the user has deliberately made small. One setting rather
    /// than one per window-led thing: the question they all ask is "is this
    /// window a small one", and two answers to it would show as one part of the
    /// interface adapting a column before another.
    ///
    /// Read on every frame, like the settings that use it: a window can be
    /// dragged.
    int adaptiveUiThreshold{80};

    /// Whether the screens show the Back button in their top-left corner, from
    /// `back_button`. `when_narrow` unless the config says otherwise, the same
    /// way the menu button is and against the same threshold: it is the only part
    /// of the interface that says out loud how to get out, and a narrow window
    /// is where saying it is worth the corner it costs. Esc goes back whether
    /// the button is there or not.
    Visibility backButton{Visibility::WhenNarrow};

    /// Whether the last row of the screen carries the hint bar — the commands
    /// of whichever screen is up, each behind the key that runs it — from
    /// `hint_bar`. `on` unless the config says otherwise: there is no help
    /// screen, one quiet row stands in for one, and the window that has least
    /// room for it is the window that needs it most — narrow is where the
    /// buttons go and the keys are all that is left. A window too narrow for
    /// the whole row carries as many hints as fit whole and leaves the rest off
    /// the end of it — see `hint_bar::render()`; `when_wide` is still there for
    /// whoever would rather have the row whole or not at all.
    ///
    /// The row is taken off the screen above it whether or not there is
    /// anything to put in it, so that the message list, whose list of hints
    /// starts empty, is not a row taller than the screens on either side of it.
    Visibility hintBar{Visibility::On};

    /// Whether the editor shows the delete-line button down its three rightmost
    /// columns, from `compose_delete_line_button` — a box around the row the
    /// cursor is on with a cross in it, which clicking deletes that line as
    /// `Ctrl-Y` does.
    ///
    /// `when_narrow` unless the config says otherwise, the same way the back and
    /// menu buttons are and against the same threshold: it is a chord's worth of
    /// work put where a pointer can reach it, and a narrow window is where the
    /// pointer is likelier to be what is to hand.
    ///
    /// It costs the text three columns on **every** row and not only on the row
    /// it stands beside, since the button walks the message with the cursor and
    /// text laid out to the full width elsewhere would rewrap under it at every
    /// keystroke. The scrollbar stands in the last of those three columns rather
    /// than asking for one of its own.
    Visibility composeDeleteLineButton{Visibility::WhenNarrow};

    /// Whether the header block carries the Recd row — when the message arrived
    /// here — under the Date row saying when it was written, from
    /// `show_recd_date`.
    ///
    /// `off` by default: the two stamps are rarely far apart, and the row is a
    /// fifth of the header block and a row off the message under it. `on` puts
    /// it up whatever the window is, and `when_wide`/`when_narrow` read the
    /// same line every other window-led setting does — a tall window is not
    /// what is measured, but a wide one is the one with room to spare.
    ///
    /// The same row in the reader and in the editor, so the two blocks go on
    /// lining up field for field; what the editor shows there is this moment,
    /// a message being written arriving here as it is stored.
    Visibility showRecdDate{Visibility::Off};

    /// Whether the reader says where the message was written, from
    /// `show_location`: the sender's location as the nodelist gives it, standing
    /// in the rule that closes the header block and lined up under the
    /// addresses above it.
    ///
    /// On by default. It costs no row — the rule is there either way — and
    /// where a message is from is the one thing about its sender that the
    /// header cannot otherwise say. It is drawn in the kludges' color for the
    /// same reason they are: it is there to be glanced at, and it is not the
    /// message.
    ///
    /// A message from an address no compiled nodelist holds shows nothing, and
    /// so does a config with no nodelist at all: the setting says whether to
    /// look, not what to invent. A point no pointlist here lists is placed where
    /// its boss is — see `nodelist::NodelistDb::findOrBoss`, and the reason: a
    /// point is that node's own client, and a pointlist is the file a system is
    /// likeliest not to have.
    bool showLocation{true};

    /// Whether the reader says how much message there is, from
    /// `reader_show_message_size`: the size of the message standing at the left
    /// end of the rule that closes the header block, where the header rows
    /// begin.
    ///
    /// Off by default. It costs no row for the same reason the location in the
    /// other end of the rule does not — the rule is there either way — but how
    /// long a message is, is a thing few readers want said about every message
    /// they open, and the reader that does want it says so once here. It is
    /// written as a bare count of bytes under a kilobyte and in kilobytes or
    /// megabytes above it
    /// — how long a message is, is a thing to be seen at a glance and never to
    /// the byte — and drawn in the kludges' color, since it is about the
    /// message and is not the message.
    ///
    /// What is counted is the message as it stands here: every line it holds,
    /// service lines included, in the UTF-8 everything above the message-base
    /// port is. Not the bytes the base keeps it in, which are the storage's own
    /// — the same message written in CP866 takes fewer of them.
    bool readerShowMessageSize{false};

    /// Whether the reader and the editor show the menu button in their top-right
    /// corner, from `menu_button` — `≡` in a box two rows tall, which clicking
    /// opens the screen's context menu.
    ///
    /// `when_narrow` unless the config says otherwise, the same way
    /// `back_button` is and against the same threshold: a menu is what a pointer
    /// is for, and a narrow window is the one where the pointer is likelier to
    /// be what is to hand. Every command in it is one a key does as well, so the
    /// corner can go without anything going with it. The area list and the
    /// message list carry no such button: neither has a command worth a menu
    /// yet.
    Visibility menuButton{Visibility::WhenNarrow};

    /// What the two menus hold, in the order they are to stand — from
    /// `reader_menu` and `compose_menu`. Each names commands of its own screen,
    /// by the part of the name after the dot: `reply_elsewhere` is
    /// `reader.reply_elsewhere`.
    ///
    /// The reader's default leaves out `change`, `info`, `export` and
    /// `comment_reply`: writing over a message that is already in a base is a
    /// rare thing to want and a bad thing to do by accident, what a base holds
    /// about a message is a question most readers never ask, a message is
    /// written out to a file now and then, and answering the recipient rather
    /// than the sender is a thing wanted now and then and never by accident —
    /// `c`/F2, `i`, `w` and Alt-Q do all four without a button.
    std::vector<Command> readerMenu{Command::ReaderList,           Command::ReaderReply,
                                    Command::ReaderReplyElsewhere, Command::ReaderNew,
                                    Command::ReaderForward,        Command::ReaderFind,
                                    Command::ReaderNodelist,       Command::ReaderShell};

    /// The editor's two: storing the message and reading a file into it. The
    /// rest of what it does is editing, which is the keyboard's — Ctrl-Y and
    /// Ctrl-D are chords anyone writing mail knows by heart, and a menu is no
    /// place to take a line out from.
    std::vector<Command> composeMenu{Command::ComposeSave, Command::ComposeImport};

    /// Whether the hint bars are written in capitals, from
    /// `hint_bar_capitalize`: `Q Reply  Ctrl-F Find  F7 Export` where they are,
    /// `q reply  ctrl-f find  f7 export` where they are not.
    ///
    /// Off unless the config says otherwise. The row is the quiet one at the
    /// bottom of the screen — it is there to be glanced at rather than read, and
    /// lower case is what keeps it from competing with the message above it.
    ///
    /// One setting for every screen's row: the four of them are one row that
    /// changes as the screen does, and a bar written one way over the reader and
    /// another over the editor would read as two different things.
    ///
    /// The keys go with the words either way, which is what makes it one
    /// setting rather than two — and it is the row's own spelling of a key and
    /// not the layout's: `g` and `G` are two keys where a `keys` file is
    /// concerned, and one hint either way here.
    bool hintBarCapitalize{false};

    /// Where in the row the hints stand, from `hint_bar_align`. In the middle
    /// unless the config says otherwise: what is beside them is a rule and not
    /// a margin, so the row closes the screen at the bottom wherever they are,
    /// and the middle is where the eye passes over them on its way down the
    /// message rather than having to go to a corner for them.
    ///
    /// One setting for every screen's row, as the case is: the four rows are one
    /// row that changes with the screen, and hints that jumped from one side to
    /// the other between two screens would read as something having moved.
    HintAlign hintBarAlign{HintAlign::Center};

    /// What each screen's hint bar names, in the order the hints are to stand —
    /// from `arealist_hints`, `msglist_hints`, `reader_hints` and
    /// `compose_hints`, each naming commands of its own screen the way the
    /// menus do, and `app.quit` besides, which every screen answers.
    ///
    /// Not everything a screen answers, by default: the row is a reminder of
    /// what there is to do here, and the keys left out of it are the ones that
    /// are either obvious (the arrows), rarely wanted (`info`) or one letter
    /// away from what is already named (`change` beside `new`). A list is as
    /// long as the user cares to make it; what has no room left in the window
    /// is left off the end of the row rather than squeezed.
    ///
    /// The message list starts with an empty row: every key on it moves the
    /// cursor. The row is still taken off the screen above it — see `hintBar` —
    /// so a list that names nothing is a rule and not a row less.
    std::vector<Command> arealistHints{Command::AreaListNextUnread,
                                       Command::AreaListRescan};
    std::vector<Command> msglistHints;
    std::vector<Command> readerHints{Command::ReaderReply,  Command::ReaderReplyElsewhere,
                                     Command::ReaderNew,    Command::ReaderList,
                                     Command::ReaderExport, Command::ReaderNodelist};
    std::vector<Command> composeHints{Command::ComposeSave, Command::ComposeDeleteLine,
                                      Command::ComposeImport};

    /// How wide one button of a menu stands, in columns, from
    /// `menu_buttons_width` — the frame around the label included, so it is the
    /// whole of what the button takes on the screen.
    ///
    /// Twenty-two by default, which holds the longest label either menu offers
    /// — `↪ Reply elsewhere`, the glyph column in front of it included — with a
    /// column or two left over, so that no button's word is set against the
    /// frame. It is stated rather than measured: a column of buttons cut
    /// to whichever of them happened to be in the menu would be a different
    /// width in every area, and the button under the pointer would move as the
    /// menu was opened again. A label with no room left for it is cut, as every
    /// other label in the interface is.
    int menuButtonsWidth{22};

    /// How long a click is shown before it is acted on, in milliseconds — the
    /// button under the pointer drawn inverted, the row under it drawn as the
    /// current one. 100ms by default, which is long enough to be seen and short
    /// enough not to feel like a delay; 0 turns the animation off and a click
    /// acts at once, as it did before there was one.
    ///
    /// Only a click is answered this way. A key does not need it: the keyboard
    /// moves the cursor first and acts second anyway, so what Enter is about to
    /// act on has been on the screen all along.
    int clickAnimationMs{100};

    /// Whether the wheel is counted against the height of a row in the lists
    /// whose rows can stand more than one line tall, from `list_wheel_throttle`.
    /// On unless the config says otherwise.
    ///
    /// A notch of the wheel is a line, and a list drawn from a format with a
    /// `\n` in it answers each of them with a whole row — two or three lines of
    /// movement for one line of wheel, so the list runs away under a flick that
    /// moves anything else on the screen a few lines. With this on, the first
    /// notch of a run moves the cursor and the rest of that row's worth are
    /// swallowed: a two-line row costs two notches and a three-line row three.
    /// A single-line format is untouched either way — there is nothing to
    /// count — which is why this is one setting for every list rather than one
    /// per list.
    bool listWheelThrottle{true};

    /// How far apart two notches may be and still count as one run of the
    /// wheel, in milliseconds, from `list_wheel_throttle_ms`. 200 by default,
    /// which is slower than any flick and faster than scrolling by hand.
    ///
    /// Anything slower than this is somebody moving the list deliberately, and
    /// every notch of it moves the cursor: what is being made proportionate is
    /// the fast wheel. Zero turns the counting off as surely as
    /// `list_wheel_throttle off` does, there being no run for a notch to belong
    /// to.
    int listWheelThrottleMs{200};

    /// For how long the tail of a flick of the wheel is kept off what has just
    /// come up in front of the user, in milliseconds, from `wheel_settle_ms`.
    /// 1500 by default.
    ///
    /// A notch arrives after the hand that asked for it — a trackpad goes on
    /// reporting them once the finger has left it — so the ones still coming
    /// when Escape closes the reader, when a box opens over it or when → walks
    /// to the next message were aimed at what has just been put away. They are
    /// swallowed while they keep arriving, and this is how long that may go on
    /// for: a tail dies out well inside it, and past it a hand that keeps
    /// turning the wheel gets what is now in front of it. Zero turns the whole
    /// of it off, and the tail lands where it falls.
    int wheelSettleMs{1500};

    /// How every stamp the reader shows is written, from
    /// `reader_datetime_format`: the two in the header block — when the message
    /// was written and when it arrived — and the one beside each answer in the
    /// replies dialog, along with the file stamps in the import dialog and the
    /// one at the head of an exported message. A strftime format, and one
    /// setting for the date and the time both because each of those is one
    /// string on screen.
    ///
    /// The message list's Date column is written with this too, and is the one
    /// place that may say otherwise: `msglist_format`'s `d(...)` gives that
    /// column a format of its own, which is what both defaults do — a column
    /// beside three others wants less of a stamp than a row of its own does.
    ///
    /// The default is the FTN stamp without its seconds and with the zone it
    /// was written in: a message is read for when it was written, not for the
    /// second it was written in, and a stamp from another zone says nothing
    /// without one. Every column showing one is measured rather than fixed, so
    /// the format may be as long as it likes.
    ///
    /// `%z` writes the offset the message's own TZUTC states and nothing where
    /// it states none — see `domain::MessageHeader::utcOffset`. It is the
    /// written stamp that carries one; the arrival stamp beside it is this
    /// system's clock, which the message has nothing to say about. So the
    /// default writes an offset beside the one stamp and a trailing blank
    /// beside the other, which is what a message stating no zone gets too.
    std::string readerDateTimeFormat{"%d %b %y %H:%M %z"};

    /// What the template's date and time tokens are written with, from
    /// `template_date_format` and `template_time_format`: `@cdate`/`@ctime` for
    /// the message being written, `@odate`/`@otime` for the one being answered.
    ///
    /// Two settings rather than one, because they are two tokens: a template
    /// written for GoldED says "@odate @otime, @oname wrote", and each half has
    /// to be able to change without the other. Both are strftime formats and
    /// the same stamp is behind them, so a format may hold anything — the
    /// tokens' names are what they are usually used for, not a limit.
    std::string templateDateFormat{"%d %b %y"};
    std::string templateTimeFormat{"%H:%M"};

    /// Theme file to draw the interface with. Empty means the built-in palette,
    /// which is what most configs will say by saying nothing.
    std::string themePath;

    /// What a quoted line is prefixed with, from `quote_string`.
    /// The default is GoldED's, where the letters before the '>' stand for the
    /// initials of whoever is being quoted; putting them there is the business
    /// of composing, which is why this is only checked and stored for now.
    ///
    /// Exactly one '>' — it is the mark every FTN reader counts quote levels
    /// by, ours included (ui::quoteDepth in ui/text_layout.hpp), so a second
    /// one would make our own first-level quote read as a second-level one.
    std::string quoteString{" FL> "};

    /// The column quoted text is wrapped at, from `quote_margin`.
    /// 78 by default: it keeps a line inside the 79 characters FTN messages
    /// have always been written to, with a character in hand for the next
    /// quote level when the message is quoted back.
    int quoteMargin{78};

    /// The lines standing either side of a file imported into a message as
    /// text, from `import_begin` and `import_end` — "=== Cut ===" both unless
    /// the config says otherwise, which is what FTN mail has always fenced an
    /// enclosed file off with.
    ///
    /// Two settings rather than one because they are two lines: a fence saying
    /// "cut here" at the top and "and here" at the bottom is a thing people
    /// write. An empty one writes no line at all, so `import_begin ""` is how a
    /// file goes in with nothing in front of it.
    ///
    /// Only text is fenced. A file imported as UUE carries its own `begin` and
    /// `end`, and a cut line among those is one more line for whoever decodes
    /// it to trip over.
    std::string importBegin{"=== Cut ==="};
    std::string importEnd{"=== Cut ==="};

    /// What the tearline says after "--- ", from `tearline`, and what the
    /// origin line says between " * Origin: " and the address in brackets, from
    /// `origin`. Both are expanded as a template line, which is how the default
    /// names the program without repeating its version here.
    ///
    /// The origin text is empty by default: it is the writer's own words about
    /// their system, and there is nothing for us to invent. The line itself is
    /// written either way — an echomail message without one is a message a
    /// tosser may refuse.
    std::string tearline{"@longpid @version"};
    std::string origin;

    /// The file a new message starts from, from `template`. Required:
    /// a template is a file we cannot invent a default for.
    ///
    /// `loadFromFile` insists on it and reads it, so a config naming no template
    /// — or one that is not there — stops the program at startup rather than at
    /// the moment someone sits down to write. `loadFromString` does not: it
    /// parses a config without standing in for the machine one would run on.
    std::string templatePath;

    /// The directory the config was read from, which is where a file named
    /// without a path is looked for — the `@file` a `CC:` or an `XC:` command
    /// may name its recipients in. Empty for a config that was not read from a
    /// file at all, and then such a name is a path of its own, relative to
    /// wherever AmberEdit was started.
    std::string configDir;

    /// The user's name and address, which every config states: they are what a
    /// message is written from, and either one missing is a message that goes
    /// out unsendable. The address stays an optional because parsing it is what
    /// fills it in, but a config that loaded has one.
    std::string userName;
    std::optional<domain::FtnAddress> userAddress;

    /// The `aka` and `akamatch` lines: the AKAs beyond userAddress, each with
    /// the destinations it is written from. Read and checked at startup; it
    /// takes effect when composing netmail exists.
    ///
    /// Every address an area group states is added here too, as an AKA with no
    /// patterns — which is what an `aka` line on its own already means, and what
    /// makes a message written from a group's address one of ours wherever it is
    /// read. It is never chosen by destination: the area it belongs to is what
    /// picks it.
    std::vector<AkaMatch> akaMatches;

    /// The `address_macro` lines, in the order they were written. A netmail
    /// recipient typed as one of these words is expanded into the whole
    /// recipient it names; see `addressMacroFor()`.
    std::vector<AddressMacro> addressMacros;

    /// The `group ... endgroup` blocks, in the order they were written.
    std::vector<AreaGroup> areaGroups;

    /// The `area ... endarea` blocks, in the order they were written: the areas
    /// this config declares itself, beside whatever the tosser's declares.
    ///
    /// They are the whole area list where there is no `tosser_config` at all,
    /// and an addition to it where there is — a local base one keeps oneself is
    /// no business of the tosser's, and neither is a directory of *.msg somebody
    /// else fills.
    std::vector<ManualArea> manualAreas;

    /// The groups covering this area, least specific first — the order
    /// `effectiveFor()` applies them in, and what AreaManager asks when it wants
    /// to know whether any of them states an address.
    [[nodiscard]] std::vector<const AreaGroup*> groupsFor(
        const domain::AreaConfig& area) const;

    /// This config as it stands in `area`: a copy of it, with every group
    /// covering the area applied over it, the more specific last. That order is
    /// the whole of what "the settings merge one by one" means — an area under
    /// three groups takes each setting from the most specific group that states
    /// that setting, and a group saying nothing about one leaves it alone.
    ///
    /// A copy rather than a view: the settings are read from a dozen places that
    /// would each otherwise have to ask twice — "is there a group, and what does
    /// it say" — and the copy is made when the area changes rather than while
    /// anything is drawn. It carries the groups still, so resolving it again for
    /// the same area answers the same thing and there is no way to hold it
    /// wrong.
    ///
    /// Only the tag is looked at today. The whole area is taken because a group
    /// selecting on anything else would be a key inside the block and no change
    /// here.
    [[nodiscard]] AppConfig effectiveFor(const domain::AreaConfig& area) const;

    /// Which of our addresses a netmail to `dest` is written from: the AKA of
    /// the matching pattern that pins down the most of the address, or
    /// userAddress when nothing matches. Nullopt only when there is no
    /// `address` line either.
    [[nodiscard]] std::optional<domain::FtnAddress> akaFor(
        const domain::FtnAddress& dest) const;

    /// The AKA `akamatch` gives for `dest`, and nothing when no pattern covers
    /// it. This is what an editor asks: a destination no rule mentions leaves
    /// the sender alone rather than pulling it back to the main address, which
    /// would undo the AKA the area itself is presented under.
    [[nodiscard]] std::optional<domain::FtnAddress> akaMatching(
        const domain::FtnAddress& dest) const;

    /// The macro `typed` is, or nullptr when it is none of them — which is what
    /// an ordinary recipient's name answers, and what leaves the nodelist to be
    /// asked about it instead.
    ///
    /// The whole of what was typed has to be the macro, trimmed of the blanks
    /// around it and read without regard to case. Matching inside a name would
    /// make every macro a word nobody could write to: an `af` found in `Olaf`
    /// would address the message to the robot.
    [[nodiscard]] const AddressMacro* addressMacroFor(std::string_view typed) const;

    /// Whether the address is one of ours — the `address` line or one of the
    /// AKAs. Compared over the four numbers only: nothing in a message base
    /// carries a domain, so an address out of one could never match a 5D
    /// spelling in the config.
    [[nodiscard]] bool isOwnAddress(const domain::FtnAddress& addr) const;

    /// Reads a config file, or says why it could not be read or parsed, or
    /// which required field is missing.
    [[nodiscard]] static tl::expected<AppConfig, ErrorPtr> loadFromFile(
        const std::string& path);

    /// Parses a config from a string — the entry point used by the tests.
    [[nodiscard]] static tl::expected<AppConfig, ErrorPtr> loadFromString(
        const std::string& text, const std::string& originName = "<string>");

    /// Paths searched when no config is given on the command line:
    /// $AMBEREDIT_CONFIG, ./amberedit.cfg, ~/.ambereditrc.
    /// Returns the first one that exists, or nullopt.
    static std::optional<std::string> findDefaultConfigPath();
};

}  // namespace amberedit::config
