#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/app_config.hpp"
#include "domain/area.hpp"
#include "domain/ftn_address.hpp"

/// The two commands a message being written may carry in its own text: `CC:`,
/// which sends a copy of it to somebody else, and `XC:`/`XP:`, which posts it
/// in other echoes as well. They are written where the message is written —
/// there is no dialog for either — and are carried out when it is stored,
/// GoldED's way and in GoldED's spelling, so that a template or a habit brought
/// from there goes on working.
///
/// What lives here is everything about them that is decided by the text alone:
/// finding the lines, reading the tokens off them, completing an address that
/// was written in part, and building the list the message keeps in place of the
/// commands. Who a name belongs to is not here — that is the nodelist's answer,
/// and the nodelist stands beside the adapters where nothing in the core may
/// reach it.
namespace amberedit::app {

/// Which of the two a line is.
enum class CopyKind {
    Carbon,     ///< `CC:` — a copy to another recipient
    Crosspost,  ///< `XC:` or `XP:` — the same message in another echo
};

/// One thing a command line names: a recipient for `CC:`, an echotag mask for
/// `XC:`.
struct CopyToken {
    /// What was written, trimmed — the `#` and, for an address, the domain
    /// after the `@` taken off.
    std::string text;
    /// Whether it was written with a `#` in front of it: the copy is made and
    /// the name is left out of the list the message keeps. A crosspost mask
    /// written that way covers its areas and names none of them.
    bool hidden{false};
};

/// One `CC:`/`XC:`/`XP:` line of a message being written.
struct CopyCommand {
    CopyKind kind{CopyKind::Carbon};
    /// Which line of the text it stands on, counted from zero.
    size_t line{0};
    std::vector<CopyToken> tokens;
    /// Why a `@file` on the line could not be read, empty when nothing went
    /// wrong. A line that names a file nobody can read is left standing in the
    /// message, as one naming a recipient nobody can find is.
    std::string error;
};

/// Every command line in the text of a message being written, in the order they
/// stand in it.
///
/// A command has to **begin** its line, without regard to case, and only an
/// ordinary line of the text can be one: the control lines, the tearline and
/// the origin closing the message, and anything carrying a quote prefix are
/// stepped over. What somebody quoted is what they wrote, not an instruction to
/// this message.
///
/// `fileDir` is where a `@file` token is looked for when it names no directory
/// of its own — the directory the config was read from.
[[nodiscard]] std::vector<CopyCommand> findCopyCommands(
    const std::vector<std::string>& lines, const std::string& fileDir);

/// Whether the line begins with one of the three prefixes — the same test
/// `findCopyCommands` makes, without reading the rest of the line.
[[nodiscard]] bool isCopyCommand(std::string_view line);

/// The line with its command spoiled: `CC: Ivan` becomes `!CC: Ivan`.
///
/// What is carried into a message from somewhere else — a message being
/// forwarded, a file being imported — is text and not commands of the message
/// being written. The prefix is what is disarmed, so that a line carrying
/// recipients is disarmed along with a bare one; the line stays readable, and
/// whoever meant to send those copies still can by taking the `!` off.
[[nodiscard]] std::string disarmCopyCommand(const std::string& line);
void disarmCopyCommands(std::vector<std::string>& lines);

/// A `CC:` token split into the two things it may hold: `Ivan Ivanov
/// 2:5020/1234` is a name and an address, `2:5020/1234` is an address alone,
/// and `Ivan Ivanov` is a name alone.
///
/// The address is the last word of the token and only where that word looks
/// like one; it is handed back as it was written, since completing it needs an
/// area's own address and that is the caller's to know.
struct WrittenRecipient {
    std::string name;
    std::string address;
};
[[nodiscard]] WrittenRecipient readRecipient(std::string_view token);

/// Whether the text is an address, whole or in part — what tells the two halves
/// of a `CC:` token apart.
[[nodiscard]] bool looksLikeAddress(std::string_view text);

/// The address the text names, with whatever it leaves out taken from `area` —
/// the AKA the message is being written under.
///
/// `2:5020/1234` states the whole of itself; `5020/1234` states everything but
/// the zone, `/1234` the node alone and `.5` the point alone. Nullopt for text
/// that is no address at all, and for one that names a node without saying
/// enough for `area` to finish it.
[[nodiscard]] std::optional<domain::FtnAddress> completeAddress(
    std::string_view text, const domain::FtnAddress& area);

/// A recipient a `CC:` token turned out to name.
struct CarbonCopy {
    std::string name;
    domain::FtnAddress address;
    bool hidden{false};
};

/// An echo an `XC:` mask turned out to name.
struct Crosspost {
    domain::AreaConfig area;
    bool hidden{false};
};

/// What one mask came to.
struct MaskResult {
    /// Whether it covered any area at all — including one another mask had
    /// already taken, and including the area being written in. A mask that
    /// covered none named nothing, which is the only failure here; one that
    /// named an echo twice over is not a mistake, it is two ways of saying the
    /// same echo.
    bool matched{false};
    /// How many areas it added to the list.
    size_t added{0};
    /// Whether it covered the area the message is being written in. That area
    /// is never crossposted to — it is where the message is going anyway — and
    /// a mask written with a `#` in front of it is what says the list need not
    /// mention it either.
    bool current{false};
};

/// Adds the areas `mask` names to `into`, and says what it came to.
///
/// The mask is matched against the whole echotag, `*` and `?` standing for what
/// they stand for everywhere else in the config. Netmail areas are left out —
/// a crosspost goes to an echo, and a netmail area holds messages addressed to
/// somebody — and an area already on the list is not added twice, whichever
/// mask reached it first.
MaskResult addCrossposts(const CopyToken& mask,
                         const std::vector<domain::AreaConfig>& areas,
                         const domain::AreaConfig& current, std::vector<Crosspost>& into);

/// The lines the message keeps where its `CC:` lines stood, in the shape
/// `compose_cc_list` asks for. Empty for the two values that leave nothing —
/// `keep`, where the command line itself stays, and `remove`.
///
/// `margin` is the column the names are wrapped at, `hidden` recipients are
/// left out, and a list of nobody is no list at all.
[[nodiscard]] std::vector<std::string> carbonLines(const std::vector<CarbonCopy>& copies,
                                                   config::CarbonList mode, int margin);

/// The same list as control lines, which is what `hidden` asks for: one
/// `CC: <name> <address>` per recipient, without the leading ^A a
/// `MessageDraft` adds for itself.
[[nodiscard]] std::vector<std::string> carbonKludges(
    const std::vector<CarbonCopy>& copies);

/// The lines the message keeps where its `XC:` lines stood, in the shape
/// `compose_xc_list` asks for.
///
/// `originally` is the echo the message is being written in, and it is named
/// only where there is somewhere else for the message to have gone: a mask that
/// matched nothing but the area it was written in leaves the message where it
/// always was, and saying "originally in" about it would be telling the reader
/// something that did not happen. An empty `originally` leaves the line out,
/// which is what a `#` on that mask asks for.
[[nodiscard]] std::vector<std::string> crosspostLines(const std::vector<Crosspost>& areas,
                                                      const std::string& originally,
                                                      config::CrosspostList mode,
                                                      int margin);

/// The text with the commands taken out and the lists put in.
///
/// `keep` is which command lines are to be left standing — the ones whose
/// recipients or areas nobody could find, and every one of them where the
/// config asks for `keep`/`raw`. Each list goes in where the first line of its
/// own kind stood, so that a message with something to say under its `CC:` line
/// keeps saying it under the list.
[[nodiscard]] std::vector<std::string> rewriteCopyCommands(
    const std::vector<std::string>& lines, const std::vector<CopyCommand>& commands,
    const std::vector<size_t>& keep, const std::vector<std::string>& carbon,
    const std::vector<std::string>& crossposts);

/// The text without the tearline and origin closing it, which is what a copy of
/// the message is built from: every area it is written into closes it with a
/// pair of its own.
[[nodiscard]] std::vector<std::string> withoutTrailer(std::vector<std::string> lines);

}  // namespace amberedit::app
