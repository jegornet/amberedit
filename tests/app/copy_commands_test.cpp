#include <catch2/catch.hpp>

#include <fstream>
#include <string>
#include <vector>

#include "app/copy_commands.hpp"
#include "config/app_config.hpp"
#include "domain/area.hpp"
#include "domain/ftn_address.hpp"
#include "temp_dir.hpp"

using amberedit::app::CarbonCopy;
using amberedit::app::CopyKind;
using amberedit::app::CopyToken;
using amberedit::app::Crosspost;
using amberedit::config::CarbonList;
using amberedit::config::CrosspostList;
using amberedit::domain::AreaConfig;
using amberedit::domain::AreaKind;
using amberedit::domain::FtnAddress;

namespace app = amberedit::app;

namespace {

/// The tokens one command line holds, as text — what nearly every case here is
/// about, the line being the only input.
std::vector<std::string> tokensOf(const std::string& line, const std::string& dir = "") {
    const auto commands = app::findCopyCommands({line}, dir);
    if (commands.empty()) return {};
    std::vector<std::string> out;
    out.reserve(commands.front().tokens.size());
    for (const auto& token : commands.front().tokens) out.push_back(token.text);
    return out;
}

AreaConfig echoAt(const std::string& tag) {
    AreaConfig area;
    area.tag = tag;
    area.kind = AreaKind::Echo;
    return area;
}

std::vector<std::string> tagsOf(const std::vector<Crosspost>& areas) {
    std::vector<std::string> out;
    out.reserve(areas.size());
    for (const auto& area : areas) out.push_back(area.area.tag);
    return out;
}

CarbonCopy copyTo(const std::string& name, const std::string& address,
                  bool hidden = false) {
    return CarbonCopy{name, *FtnAddress::parse(address), hidden};
}

/// A file of recipients beside the test, which is what a `@file` token names.
std::string fileWith(const amberedit::test::TempDir& dir, const std::string& name,
                     const std::string& content) {
    const std::string path = dir.path(name);
    std::ofstream out(path);
    out << content;
    return path;
}

}  // namespace

TEST_CASE("A command is the word the line begins with, whatever its case",
          "[copy][commands]") {
    const std::vector<std::string> lines{
        "CC: Ivan Ivanov", "cc: Vasya Pupkin",      "Xc: ru.linux", "XP: ru.unix",
        " CC: indented",   "see CC: in the middle", "CCX: not it",
    };
    const auto commands = app::findCopyCommands(lines, "");
    REQUIRE(commands.size() == 4);
    CHECK(commands[0].kind == CopyKind::Carbon);
    CHECK(commands[0].line == 0);
    CHECK(commands[1].kind == CopyKind::Carbon);
    // XP: is XC: under another name, which is what GoldED takes and what a
    // habit brought from there writes.
    CHECK(commands[2].kind == CopyKind::Crosspost);
    CHECK(commands[3].kind == CopyKind::Crosspost);
    CHECK(commands[3].line == 3);
}

TEST_CASE("Kludges, the closing pair and quotes carry no commands", "[copy][commands]") {
    const std::vector<std::string> lines{
        "\001CC: Ivan Ivanov", " IK> CC: Ivan Ivanov",       ">> CC: Ivan Ivanov",
        "--- CC: Ivan Ivanov", " * Origin: CC: Ivan Ivanov",
    };
    CHECK(app::findCopyCommands(lines, "").empty());
}

TEST_CASE("A CC: line is separated by commas alone", "[copy][commands]") {
    // A name holds spaces and does not hold commas, so the comma is the only
    // thing that can end one.
    CHECK(tokensOf("CC: Ivan Ivanov, Vasya Pupkin") ==
          std::vector<std::string>{"Ivan Ivanov", "Vasya Pupkin"});
    CHECK(tokensOf("CC:   Ivan Ivanov  ,Vasya Pupkin  ") ==
          std::vector<std::string>{"Ivan Ivanov", "Vasya Pupkin"});
}

TEST_CASE("An XC: line is separated by commas, spaces and tabs", "[copy][commands]") {
    CHECK(tokensOf("XC: ru.* ru.linux,\tde.talk") ==
          std::vector<std::string>{"ru.*", "ru.linux", "de.talk"});
}

TEST_CASE("A '#' hides what it stands in front of", "[copy][commands]") {
    const auto commands = app::findCopyCommands({"CC: #Ivan Ivanov, Vasya Pupkin"}, "");
    REQUIRE(commands.size() == 1);
    REQUIRE(commands.front().tokens.size() == 2);
    CHECK(commands.front().tokens[0].hidden);
    CHECK(commands.front().tokens[0].text == "Ivan Ivanov");
    CHECK_FALSE(commands.front().tokens[1].hidden);
}

TEST_CASE("The domain of a 5D address is dropped", "[copy][commands]") {
    // Nothing in a message base carries a domain, so there is nothing for one
    // to be matched against.
    CHECK(tokensOf("CC: 2:5020/1234@fidonet") == std::vector<std::string>{"2:5020/1234"});
    // A name is left alone: only an address, which is what the colon says, is
    // trimmed of anything.
    CHECK(tokensOf("CC: Ivan Ivanov") == std::vector<std::string>{"Ivan Ivanov"});
}

TEST_CASE("A @file names the recipients instead of the line", "[copy][commands]") {
    amberedit::test::TempDir dir;
    fileWith(dir, "team.lst",
             "Ivan Ivanov, Vasya Pupkin\n"
             "#Pyotr Petrov\n"
             "@other.lst\n");
    fileWith(dir, "other.lst", "Nobody At All\n");

    const auto commands =
        app::findCopyCommands({"CC: @team.lst, Anna Karenina"}, dir.path(""));
    REQUIRE(commands.size() == 1);
    const auto& tokens = commands.front().tokens;
    // Three out of the file and the one written on the line itself. The `@`
    // inside the file is passed over: a list that could name another list is a
    // list that could name itself.
    REQUIRE(tokens.size() == 4);
    CHECK(tokens[0].text == "Ivan Ivanov");
    CHECK(tokens[1].text == "Vasya Pupkin");
    CHECK(tokens[2].text == "Pyotr Petrov");
    CHECK(tokens[2].hidden);
    CHECK(tokens[3].text == "Anna Karenina");
    CHECK(commands.front().error.empty());
}

TEST_CASE("A @file that cannot be read is said so, not passed over", "[copy][commands]") {
    amberedit::test::TempDir dir;
    const auto commands = app::findCopyCommands({"CC: @nothing.lst"}, dir.path(""));
    REQUIRE(commands.size() == 1);
    CHECK(commands.front().tokens.empty());
    CHECK_FALSE(commands.front().error.empty());
}

TEST_CASE("A command is disarmed by its prefix, not by the whole line",
          "[copy][commands]") {
    // The defect this is written against: a line carrying recipients has to be
    // disarmed exactly as a bare one is.
    CHECK(app::disarmCopyCommand("CC: Ivan Ivanov") == "!CC: Ivan Ivanov");
    CHECK(app::disarmCopyCommand("CC:") == "!CC:");
    CHECK(app::disarmCopyCommand("xp: ru.linux") == "!xp: ru.linux");
    CHECK(app::disarmCopyCommand("Occasionally CC: matters") ==
          "Occasionally CC: matters");

    std::vector<std::string> lines{"CC: Ivan", "plain"};
    app::disarmCopyCommands(lines);
    CHECK(lines == std::vector<std::string>{"!CC: Ivan", "plain"});
    // And a line already disarmed is a line that begins with '!', so it stays
    // as it is rather than growing another.
    app::disarmCopyCommands(lines);
    CHECK(lines[0] == "!CC: Ivan");
}

TEST_CASE("An address written in part is finished from the area's own",
          "[copy][address]") {
    const FtnAddress area = *FtnAddress::parse("2:5020/9999.7");

    CHECK(app::completeAddress("2:5020/1234", area)->toString() == "2:5020/1234");
    CHECK(app::completeAddress("2:5020/1234.5", area)->toString() == "2:5020/1234.5");
    CHECK(app::completeAddress("5020/1234", area)->toString() == "2:5020/1234");
    CHECK(app::completeAddress("/1234", area)->toString() == "2:5020/1234");
    CHECK(app::completeAddress(".5", area)->toString() == "2:5020/9999.5");
    // A point is stated or it is nothing: written under a point's AKA,
    // `2:5020/1234` is that node and not a point of it.
    CHECK(app::completeAddress("2:5020/1234", area)->point == 0);

    // Neither a bare number nor a name is an address: guessing at one would
    // address the copy to somebody nobody named.
    CHECK_FALSE(app::completeAddress("1234", area));
    CHECK_FALSE(app::completeAddress("Ivan Ivanov", area));
    CHECK_FALSE(app::completeAddress("2:5020/", area));
    // Nothing to finish it with, and nothing invented in its place.
    CHECK_FALSE(app::completeAddress("/1234", FtnAddress{}));
}

TEST_CASE("A CC: token may name a person and their address both", "[copy][address]") {
    const auto named = app::readRecipient("Ivan Ivanov 2:5020/1234");
    CHECK(named.name == "Ivan Ivanov");
    CHECK(named.address == "2:5020/1234");

    const auto bare = app::readRecipient("2:5020/1234");
    CHECK(bare.name.empty());
    CHECK(bare.address == "2:5020/1234");

    const auto person = app::readRecipient("Ivan Ivanov");
    CHECK(person.name == "Ivan Ivanov");
    CHECK(person.address.empty());

    // A partial address is an address here too, since the area can finish it.
    const auto partial = app::readRecipient("Ivan Ivanov /1234");
    CHECK(partial.name == "Ivan Ivanov");
    CHECK(partial.address == "/1234");
}

TEST_CASE("A mask covers every echo it matches, once", "[copy][crosspost]") {
    const std::vector<AreaConfig> areas{echoAt("ru.linux"), echoAt("ru.unix"),
                                        echoAt("de.talk"), [] {
                                            AreaConfig netmail = echoAt("netmail");
                                            netmail.kind = AreaKind::Netmail;
                                            return netmail;
                                        }()};
    const AreaConfig current = echoAt("ru.talk");

    std::vector<Crosspost> taken;
    const auto first = app::addCrossposts({"ru.*", false}, areas, current, taken);
    CHECK(first.added == 2);
    CHECK_FALSE(first.current);
    // The second mask names one of them again, and it is not taken twice.
    const auto second = app::addCrossposts({"RU.LINUX", false}, areas, current, taken);
    CHECK(second.added == 0);
    CHECK(tagsOf(taken) == std::vector<std::string>{"ru.linux", "ru.unix"});

    // Netmail is never crossposted to: the same message there would be
    // addressed to nobody.
    std::vector<Crosspost> every;
    CHECK(app::addCrossposts({"*", false}, areas, current, every).added == 3);
}

TEST_CASE("A mask covering the area being written in adds nothing and says so",
          "[copy][crosspost]") {
    const std::vector<AreaConfig> areas{echoAt("ru.linux"), echoAt("ru.talk")};
    const AreaConfig current = echoAt("ru.talk");

    std::vector<Crosspost> taken;
    const auto result = app::addCrossposts({"ru.*", true}, areas, current, taken);
    CHECK(result.added == 1);
    CHECK(result.current);
    CHECK(tagsOf(taken) == std::vector<std::string>{"ru.linux"});
}

TEST_CASE("The five shapes of a carbon copy list", "[copy][list]") {
    const std::vector<CarbonCopy> copies{copyTo("Ivan Ivanov", "2:5020/1234"),
                                         copyTo("Vasya Pupkin", "2:5020/2345"),
                                         copyTo("Pyotr Petrov", "2:5020/3456", true)};

    // Nothing at all: the line stays where it was written, or it goes and
    // nothing stands in its place.
    CHECK(app::carbonLines(copies, CarbonList::Keep, 78).empty());
    CHECK(app::carbonLines(copies, CarbonList::Remove, 78).empty());

    CHECK(app::carbonLines(copies, CarbonList::Visible, 78) ==
          std::vector<std::string>{"* Carbon copied to Ivan Ivanov  2:5020/1234",
                                   "* Carbon copied to Vasya Pupkin  2:5020/2345"});

    CHECK(app::carbonLines(copies, CarbonList::Names, 78) ==
          std::vector<std::string>{"* Carbon copied to Ivan Ivanov, Vasya Pupkin"});

    // The hidden form is control lines rather than text, so the list itself is
    // empty and the kludges carry it.
    CHECK(app::carbonLines(copies, CarbonList::Hidden, 78).empty());
    CHECK(app::carbonKludges(copies) ==
          std::vector<std::string>{"CC: Ivan Ivanov 2:5020/1234",
                                   "CC: Vasya Pupkin 2:5020/2345"});
}

TEST_CASE("A list of names too long for the line is wrapped under itself",
          "[copy][list]") {
    const std::vector<CarbonCopy> copies{copyTo("Ivan Ivanov", "2:5020/1"),
                                         copyTo("Vasya Pupkin", "2:5020/2"),
                                         copyTo("Anna Karenina", "2:5020/3")};
    const auto lines = app::carbonLines(copies, CarbonList::Names, 40);
    // Wrapped at the margin and lined up under the first name, which is where
    // the list reads as a list rather than as a paragraph.
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "* Carbon copied to Ivan Ivanov,");
    CHECK(lines[1] == "                   Vasya Pupkin,");
    CHECK(lines[2] == "                   Anna Karenina");
}

TEST_CASE("The four shapes of a crosspost list", "[copy][list]") {
    AreaConfig linux = echoAt("ru.linux");
    AreaConfig unix = echoAt("ru.unix");
    AreaConfig quiet = echoAt("ru.quiet");
    const std::vector<Crosspost> areas{{linux, false}, {unix, false}, {quiet, true}};

    CHECK(app::crosspostLines(areas, "ru.talk", CrosspostList::Raw, 78).empty());
    CHECK(app::crosspostLines(areas, "ru.talk", CrosspostList::None, 78).empty());

    CHECK(app::crosspostLines(areas, "ru.talk", CrosspostList::Yes, 78) ==
          std::vector<std::string>{"* Originally in ru.talk", "* Crossposted in ru.linux",
                                   "* Crossposted in ru.unix"});

    CHECK(app::crosspostLines(areas, "ru.talk", CrosspostList::Verbose, 78) ==
          std::vector<std::string>{"* Originally in ru.talk",
                                   "* Crossposted in ru.linux, ru.unix"});

    // No echo to have come from — a `#` on the mask that covered this one — and
    // the line is left unsaid.
    CHECK(app::crosspostLines(areas, "", CrosspostList::Verbose, 78) ==
          std::vector<std::string>{"* Crossposted in ru.linux, ru.unix"});
}

TEST_CASE("The lists stand where the first command line of their kind stood",
          "[copy][list]") {
    const std::vector<std::string> lines{
        "CC: Ivan Ivanov", "Hello there.", "XC: ru.linux", "CC: Vasya Pupkin", "Bye.",
    };
    const auto commands = app::findCopyCommands(lines, "");
    REQUIRE(commands.size() == 3);

    const auto rewritten = app::rewriteCopyCommands(
        lines, commands, {}, {"* Carbon copied to Ivan Ivanov, Vasya Pupkin"},
        {"* Crossposted in ru.linux"});
    CHECK(rewritten ==
          std::vector<std::string>{"* Carbon copied to Ivan Ivanov, Vasya Pupkin",
                                   "Hello there.", "* Crossposted in ru.linux", "Bye."});
}

TEST_CASE("A command line nobody could carry out stays in the message", "[copy][list]") {
    const std::vector<std::string> lines{"CC: Ivan Ivanov", "CC: Nobody At All", "Bye."};
    const auto commands = app::findCopyCommands(lines, "");

    // The second line named somebody nobody found: it is left exactly as it was
    // typed, so that what was written is not lost, and the list goes in where
    // the first stood.
    const auto rewritten = app::rewriteCopyCommands(
        lines, commands, {1}, {"* Carbon copied to Ivan Ivanov"}, {});
    CHECK(rewritten == std::vector<std::string>{"* Carbon copied to Ivan Ivanov",
                                                "CC: Nobody At All", "Bye."});
}

TEST_CASE("The pair closing the message comes off a copy of it", "[copy][list]") {
    const std::vector<std::string> lines{"Hello.", "", "--- AmberEdit/darwin 0.1",
                                         " * Origin: here (2:5020/1)", ""};
    CHECK(app::withoutTrailer(lines) == std::vector<std::string>{"Hello.", ""});
    // A message whose author deleted them keeps every line it has.
    CHECK(app::withoutTrailer({"Hello."}) == std::vector<std::string>{"Hello."});
}
