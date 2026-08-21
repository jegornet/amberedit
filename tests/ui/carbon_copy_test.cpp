#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/area_manager.hpp"
#include "config/app_config.hpp"
#include "domain/message.hpp"
#include "nodelist/nodelist_writer.hpp"
#include "ports/i_area_source.hpp"
#include "ports/i_lastread_store.hpp"
#include "temp_dir.hpp"
#include "test_strings.hpp"
#include "ui/app_state.hpp"
#include "ui/nodelist_dialog.hpp"
#include "ui/screens/compose_screen.hpp"

using amberedit::config::AppConfig;
using amberedit::config::CarbonList;
using amberedit::config::CrosspostList;
using amberedit::domain::AreaConfig;
using amberedit::domain::AreaKind;
using amberedit::domain::FtnAddress;
using amberedit::ui::AppState;

namespace attr = amberedit::domain::attr;
namespace compose = amberedit::ui::screens::compose;
namespace nodelist = amberedit::nodelist;
namespace nodelist_dialog = amberedit::ui::nodelist_dialog;

namespace {

/// A mark per area, as every real store keeps them.
class PerAreaLastReadStore final : public amberedit::ports::ILastReadStore {
public:
    uint32_t getLastRead(const AreaConfig& area) override {
        const auto found = marks_.find(area.tag);
        return found == marks_.end() ? 0 : found->second;
    }
    void setLastRead(const AreaConfig& area, uint32_t uid) override {
        marks_[area.tag] = uid;
    }

private:
    std::map<std::string, uint32_t> marks_;
};

class ListedAreaSource final : public amberedit::ports::IAreaConfigSource {
public:
    explicit ListedAreaSource(std::vector<AreaConfig> areas) : areas_(std::move(areas)) {}
    amberedit::Result<std::vector<AreaConfig>> loadAreas() override { return areas_; }

private:
    std::vector<AreaConfig> areas_;
};

nodelist::NodeEntry nodeOf(const std::string& address, const std::string& sysop) {
    nodelist::NodeEntry entry;
    entry.address = *FtnAddress::parse(address);
    entry.system = "A System";
    entry.location = "Somewhere";
    entry.sysop = sysop;
    entry.phone = "-Unpublished-";
    entry.speed = 300;
    return entry;
}

/// A netmail area, four echoes and a local one, each on a Squish base of its
/// own that is made the first time the area is opened — which is what an area a
/// tosser config has just declared looks like on disk.
///
/// It is the least a `CC:` and an `XC:` line can be driven through: the copies
/// go somewhere other than where the message is written, and telling whether
/// they went there means reading the base they went into.
struct CopyFixture {
    explicit CopyFixture(const std::string& groups = "")
        : config(configWith(groups)),
          lastRead(new PerAreaLastReadStore),
          manager(std::make_unique<ListedAreaSource>(areaList()),
                  std::unique_ptr<PerAreaLastReadStore>(lastRead), config),
          state(manager, config) {
        static_cast<void>(manager.reload());
    }

    [[nodiscard]] std::vector<AreaConfig> areaList() const {
        std::vector<AreaConfig> areas;
        areas.push_back(areaAt("netmail", AreaKind::Netmail));
        areas.push_back(areaAt("ru.talk", AreaKind::Echo));
        areas.push_back(areaAt("ru.linux", AreaKind::Echo));
        areas.push_back(areaAt("ru.unix", AreaKind::Echo));
        areas.push_back(areaAt("de.talk", AreaKind::Echo));
        areas.push_back(areaAt("notes", AreaKind::Local));
        return areas;
    }

    [[nodiscard]] AreaConfig areaAt(const std::string& tag, AreaKind kind) const {
        AreaConfig area;
        area.tag = tag;
        area.path = dir.path(tag);
        area.type = amberedit::domain::MsgBaseType::Squish;
        area.kind = kind;
        return area;
    }

    [[nodiscard]] AreaConfig areaNamed(const std::string& tag) const {
        for (const auto& area : areaList()) {
            if (area.tag == tag) return area;
        }
        FAIL("no such area: " << tag);
        return {};
    }

    [[nodiscard]] AppConfig configWith(const std::string& groups) const {
        AppConfig cfg;
        cfg.userName = "Yegor Gluhov";
        cfg.userAddress = FtnAddress::parse("2:5020/9999");
        cfg.composeCharset = "CP866";
        // Where a `@file` is looked for when it names no directory of its own.
        cfg.configDir = dir.path("");
        if (!groups.empty()) {
            cfg.areaGroups =
                amberedit::test::valueOf(
                    AppConfig::loadFromString("tosser_config /dev/null\n"
                                              "tosser_config_format fidoconfig\n"
                                              "default_charset CP866\n"
                                              "compose_charset CP866\n" +
                                              groups))
                    .areaGroups;
        }
        return cfg;
    }

    /// The nodelist the names in these tests are looked up in.
    void giveNodelist() {
        nodelist::DbSource source;
        source.state.spec = "nodelist";
        source.entries = {
            nodeOf("2:5020/1234", "Ivan Ivanov"),
            nodeOf("2:5020/2345", "Vasya Pupkin"),
            nodeOf("2:5020/3456", "Sergey Sergeev"),
            nodeOf("2:5020/4567", "Sergey Petrov"),
        };
        nodelist::writeNodelistDb(dir.path("nodelist.db"), {source}, 0);
        config.nodelistDbPath = dir.path("nodelist.db");
    }

    /// Enters an area, its base made if nothing stands at its path yet.
    void enter(const std::string& tag) {
        const AreaConfig area = areaNamed(tag);
        state.setCurrentArea(area);
        state.base = amberedit::test::valueOf(manager.openArea(area));
        REQUIRE(state.base != nullptr);
        state.messageCount = state.base->count();
    }

    /// Begins a message in the area on screen and puts `lines` at the head of
    /// its text, above whatever closes it.
    void begin(const std::vector<std::string>& lines) {
        compose::startNew(state);
        state.edit.lines.insert(state.edit.lines.begin(), lines.begin(), lines.end());
    }

    /// Storing it, and the question the commands in it raise. What comes back is
    /// whether the question was asked at all — a message carrying no commands is
    /// stored outright.
    bool store() {
        compose::saveMessage(state);
        if (state.confirm != AppState::Confirm::ProcessCopies) return false;
        state.confirm = AppState::Confirm::None;
        return true;
    }

    /// The whole of it: the message stored and its commands carried out.
    void storeAndProcess() {
        REQUIRE(store());
        compose::processCopies(state);
    }

    uint32_t countIn(const std::string& tag) {
        amberedit::ports::IMsgBase* base =
            amberedit::test::valueOf(manager.openArea(areaNamed(tag)));
        REQUIRE(base != nullptr);
        const uint32_t count = base->count();
        manager.closeCurrentArea();
        return count;
    }

    amberedit::domain::MessageHeader headerIn(const std::string& tag, uint32_t number) {
        amberedit::ports::IMsgBase* base =
            amberedit::test::valueOf(manager.openArea(areaNamed(tag)));
        REQUIRE(base != nullptr);
        const auto header = base->header(number);
        manager.closeCurrentArea();
        return header;
    }

    /// What a message reads as, service lines left out.
    std::vector<std::string> textIn(const std::string& tag, uint32_t number) {
        amberedit::ports::IMsgBase* base =
            amberedit::test::valueOf(manager.openArea(areaNamed(tag)));
        REQUIRE(base != nullptr);
        const auto body = base->body(number);
        manager.closeCurrentArea();

        std::vector<std::string> lines;
        for (const auto& line : body.lines) {
            if (!line.kludge) lines.push_back(line.text);
        }
        return lines;
    }

    /// And the service lines of it, as the reader shows them — '@' where the
    /// stored byte is ^A.
    std::vector<std::string> kludgesIn(const std::string& tag, uint32_t number) {
        amberedit::ports::IMsgBase* base =
            amberedit::test::valueOf(manager.openArea(areaNamed(tag)));
        REQUIRE(base != nullptr);
        const auto body = base->body(number);
        manager.closeCurrentArea();

        std::vector<std::string> lines;
        for (const auto& line : body.lines) {
            if (line.kludge) lines.push_back(line.text);
        }
        return lines;
    }

    amberedit::test::TempDir dir;
    AppConfig config;
    PerAreaLastReadStore* lastRead;
    amberedit::app::AreaManager manager;
    AppState state;
};

/// Whether any line of the message holds `what`.
bool holds(const std::vector<std::string>& lines, const std::string& what) {
    return std::any_of(lines.begin(), lines.end(), [&what](const std::string& line) {
        return line.find(what) != std::string::npos;
    });
}

}  // namespace

TEST_CASE("A CC: line writes a copy to everybody it names [copy][compose]") {
    CopyFixture fixture;
    fixture.giveNodelist();
    fixture.enter("netmail");

    fixture.begin({"CC: Ivan Ivanov, Vasya Pupkin", "Hello there."});
    fixture.state.compose.toName = "Yegor Gluhov";
    fixture.state.compose.toAddr = "2:5020/1";
    fixture.storeAndProcess();

    // The message itself and one copy per recipient, all three in netmail. The
    // message is stored first, so that a base refusing it is a message nothing
    // was copied on account of.
    REQUIRE(fixture.countIn("netmail") == 3);
    CHECK(fixture.headerIn("netmail", 1).to == "Yegor Gluhov");
    CHECK(fixture.headerIn("netmail", 2).to == "Ivan Ivanov");
    CHECK(fixture.headerIn("netmail", 2).destAddr.toString() == "2:5020/1234");
    CHECK(fixture.headerIn("netmail", 3).to == "Vasya Pupkin");
    CHECK(fixture.headerIn("netmail", 3).destAddr.toString() == "2:5020/2345");

    // A copy is a netmail of its own: local, and private as netmail has always
    // been. It carries the subject and the text of the message it copies.
    const uint32_t attributes = fixture.headerIn("netmail", 2).attributes;
    CHECK((attributes & attr::kLocal) != 0);
    CHECK((attributes & attr::kPrivate) != 0);
    CHECK(holds(fixture.textIn("netmail", 2), "Hello there."));

    // And the command is gone from all three, the list standing where it was.
    for (uint32_t number = 1; number <= 3; ++number) {
        const auto text = fixture.textIn("netmail", number);
        CHECK_FALSE(holds(text, "CC: Ivan"));
        CHECK(holds(text, "* Carbon copied to Ivan Ivanov, Vasya Pupkin"));
    }
}

TEST_CASE("A recipient written with a '#' gets the copy and is not named "
          "[copy][compose]") {
    CopyFixture fixture;
    fixture.giveNodelist();
    fixture.enter("netmail");

    fixture.begin({"CC: #Ivan Ivanov, Vasya Pupkin"});
    fixture.state.compose.toName = "Yegor Gluhov";
    fixture.state.compose.toAddr = "2:5020/1";
    fixture.storeAndProcess();

    REQUIRE(fixture.countIn("netmail") == 3);
    CHECK(fixture.headerIn("netmail", 2).to == "Ivan Ivanov");
    const auto text = fixture.textIn("netmail", 1);
    CHECK(holds(text, "* Carbon copied to Vasya Pupkin"));
    CHECK_FALSE(holds(text, "Ivan Ivanov"));
}

TEST_CASE("Nobody is sent the same message twice [copy][compose]") {
    CopyFixture fixture;
    fixture.giveNodelist();
    fixture.enter("netmail");

    fixture.begin({"CC: Ivan Ivanov, Vasya Pupkin"});
    // The message is already addressed to Ivan Ivanov, so the copy to him would
    // be the same message to the same node — a copy is made where either the
    // name or the address differs, and here neither does.
    fixture.state.compose.toName = "Ivan Ivanov";
    fixture.state.compose.toAddr = "2:5020/1234";
    fixture.storeAndProcess();

    REQUIRE(fixture.countIn("netmail") == 2);
    CHECK(fixture.headerIn("netmail", 2).to == "Vasya Pupkin");
    // The same name at another address is somebody else, and gets one.
    CHECK(holds(fixture.textIn("netmail", 1), "* Carbon copied to Vasya Pupkin"));
}

TEST_CASE("A CC: line may name an address, whole or in part [copy][compose]") {
    CopyFixture fixture;
    fixture.giveNodelist();
    fixture.enter("netmail");

    fixture.begin({"CC: 2:5020/1234", "CC: /2345", "CC: Anna Karenina 2:5020/3456",
                   "CC: 2:5020/4567@fidonet"});
    fixture.state.compose.toName = "Yegor Gluhov";
    fixture.state.compose.toAddr = "2:5020/1";
    fixture.storeAndProcess();

    // The message, and the four copies after it.
    REQUIRE(fixture.countIn("netmail") == 5);
    // A bare address is looked up, so the copy is addressed to whoever is
    // there rather than to nobody.
    CHECK(fixture.headerIn("netmail", 2).destAddr.toString() == "2:5020/1234");
    CHECK(fixture.headerIn("netmail", 2).to == "Ivan Ivanov");
    // What the line left out is the area's own AKA, 2:5020/9999.
    CHECK(fixture.headerIn("netmail", 3).destAddr.toString() == "2:5020/2345");
    // A name written in front of the address is the name the copy goes to.
    CHECK(fixture.headerIn("netmail", 4).to == "Anna Karenina");
    CHECK(fixture.headerIn("netmail", 4).destAddr.toString() == "2:5020/3456");
    // The domain of a 5D address is nothing a message base holds.
    CHECK(fixture.headerIn("netmail", 5).destAddr.toString() == "2:5020/4567");
}

TEST_CASE("A CC: line may name a file of recipients [copy][compose]") {
    CopyFixture fixture;
    fixture.giveNodelist();
    {
        std::ofstream out(fixture.dir.path("team.lst"));
        out << "Ivan Ivanov, Vasya Pupkin\n@more.lst\n";
    }
    {
        std::ofstream out(fixture.dir.path("more.lst"));
        out << "Sergey Petrov\n";
    }
    fixture.enter("netmail");

    fixture.begin({"CC: @team.lst"});
    fixture.state.compose.toName = "Yegor Gluhov";
    fixture.state.compose.toAddr = "2:5020/1";
    fixture.storeAndProcess();

    // Two out of the file, and the file it names in turn is passed over.
    REQUIRE(fixture.countIn("netmail") == 3);
    CHECK(fixture.headerIn("netmail", 2).to == "Ivan Ivanov");
    CHECK(fixture.headerIn("netmail", 3).to == "Vasya Pupkin");
}

TEST_CASE("A recipient nobody can find loses no copy and no words [copy][compose]") {
    // No nodelist at all: there is nothing to look a name up in, and nothing to
    // ask the user about either.
    CopyFixture fixture;
    fixture.enter("netmail");

    fixture.begin({"CC: Nobody At All", "Hello there."});
    fixture.state.compose.toName = "Yegor Gluhov";
    fixture.state.compose.toAddr = "2:5020/1";
    fixture.storeAndProcess();

    // The message and nothing beside it.
    REQUIRE(fixture.countIn("netmail") == 1);
    // The line is still in the message — what was written is not thrown away
    // because nobody could act on it — and the box says what was not done.
    CHECK(holds(fixture.textIn("netmail", 1), "CC: Nobody At All"));
    CHECK(fixture.state.errorMessage.find("Nobody At All") != std::string::npos);
    // It reports on a screen that is still standing, so acknowledging it leaves
    // the user there rather than on the area list.
    CHECK_FALSE(fixture.state.errorEndsScreen);
}

TEST_CASE("A name several nodes answer to is asked about [copy][compose][nodelist]") {
    CopyFixture fixture;
    fixture.giveNodelist();
    fixture.enter("netmail");

    fixture.begin({"CC: Sergey"});
    fixture.state.compose.toName = "Yegor Gluhov";
    fixture.state.compose.toAddr = "2:5020/1";
    REQUIRE(fixture.store());
    compose::processCopies(fixture.state);

    // Two Sergeys, so the nodelist box is up on what the name found and nothing
    // has been stored yet.
    REQUIRE(fixture.state.nodelistView);
    CHECK(fixture.state.nodelistView->purpose ==
          AppState::NodelistView::Purpose::PickCarbonCopy);
    CHECK(fixture.state.nodelistView->lookup == "Sergey");
    CHECK(fixture.state.nodelistView->matches.size() == 2);

    const auto node = nodelist_dialog::currentNode(fixture.state);
    REQUIRE(node);
    fixture.state.nodelistView.reset();
    compose::useCarbonCopy(fixture.state, &*node);

    REQUIRE(fixture.countIn("netmail") == 2);
    CHECK(fixture.headerIn("netmail", 2).to == node->sysop);
    CHECK(fixture.state.errorMessage.empty());
}

TEST_CASE("A box closed without picking anybody makes no copy "
          "[copy][compose][nodelist]") {
    CopyFixture fixture;
    fixture.giveNodelist();
    fixture.enter("netmail");

    fixture.begin({"CC: Sergey"});
    fixture.state.compose.toName = "Yegor Gluhov";
    fixture.state.compose.toAddr = "2:5020/1";
    REQUIRE(fixture.store());
    compose::processCopies(fixture.state);
    REQUIRE(fixture.state.nodelistView);

    // Esc closes it, which is an answer: that copy is not made.
    fixture.state.nodelistView.reset();
    compose::useCarbonCopy(fixture.state, nullptr);

    REQUIRE(fixture.countIn("netmail") == 1);
    CHECK(holds(fixture.textIn("netmail", 1), "CC: Sergey"));
    CHECK(fixture.state.errorMessage.find("Sergey") != std::string::npos);
}

TEST_CASE("Ignore stores the message with its commands as text [copy][compose]") {
    CopyFixture fixture;
    fixture.giveNodelist();
    fixture.enter("netmail");

    fixture.begin({"CC: Ivan Ivanov"});
    fixture.state.compose.toName = "Yegor Gluhov";
    fixture.state.compose.toAddr = "2:5020/1";
    REQUIRE(fixture.store());
    compose::ignoreCopies(fixture.state);

    REQUIRE(fixture.countIn("netmail") == 1);
    CHECK(holds(fixture.textIn("netmail", 1), "CC: Ivan Ivanov"));
}

TEST_CASE("An echo whose answers go nowhere leaves its CC: lines alone "
          "[copy][compose]") {
    CopyFixture fixture;
    fixture.giveNodelist();
    fixture.enter("ru.talk");

    fixture.begin({"CC: Ivan Ivanov"});
    // Nothing to ask about: without a netmail area for the copies to go into
    // the line is not a command at all.
    CHECK_FALSE(fixture.store());
    REQUIRE(fixture.countIn("ru.talk") == 1);
    CHECK(holds(fixture.textIn("ru.talk", 1), "CC: Ivan Ivanov"));
    CHECK(fixture.countIn("netmail") == 0);
}

TEST_CASE("An echo that says where its answers belong copies them there "
          "[copy][compose]") {
    CopyFixture fixture(
        "group\n"
        "  member ru.talk\n"
        "  reply_to_area netmail\n"
        "endgroup\n");
    fixture.giveNodelist();
    fixture.enter("ru.talk");

    fixture.begin({"CC: Ivan Ivanov"});
    fixture.storeAndProcess();

    CHECK(fixture.countIn("ru.talk") == 1);
    REQUIRE(fixture.countIn("netmail") == 1);
    CHECK(fixture.headerIn("netmail", 1).to == "Ivan Ivanov");
    CHECK(fixture.headerIn("netmail", 1).destAddr.toString() == "2:5020/1234");
    CHECK(holds(fixture.textIn("ru.talk", 1), "* Carbon copied to Ivan Ivanov"));
}

TEST_CASE("A local area is no place to write copies from [copy][compose]") {
    CopyFixture fixture(
        "group\n"
        "  member notes\n"
        "  reply_to_area netmail\n"
        "endgroup\n");
    fixture.giveNodelist();
    fixture.enter("notes");

    fixture.begin({"CC: Ivan Ivanov"});
    CHECK_FALSE(fixture.store());
    CHECK(holds(fixture.textIn("notes", 1), "CC: Ivan Ivanov"));
    CHECK(fixture.countIn("netmail") == 0);
}

TEST_CASE("An XC: line posts the message in every echo its masks name "
          "[copy][crosspost][compose]") {
    CopyFixture fixture;
    fixture.enter("ru.talk");

    fixture.begin({"XC: ru.* ru.linux", "Hello there."});
    fixture.storeAndProcess();

    CHECK(fixture.countIn("ru.talk") == 1);
    CHECK(fixture.countIn("ru.linux") == 1);
    CHECK(fixture.countIn("ru.unix") == 1);
    // The mask covered ru.linux twice over and de.talk not at all.
    CHECK(fixture.countIn("de.talk") == 0);
    CHECK(fixture.countIn("netmail") == 0);

    const auto text = fixture.textIn("ru.talk", 1);
    CHECK(holds(text, "* Originally in ru.talk"));
    CHECK(holds(text, "* Crossposted in ru.linux, ru.unix"));
    CHECK_FALSE(holds(text, "XC: ru.*"));

    // The copy carries the message and the same list, and closes with an origin
    // naming the AKA of the echo it went into.
    const auto copy = fixture.textIn("ru.linux", 1);
    CHECK(holds(copy, "Hello there."));
    CHECK(holds(copy, "* Crossposted in ru.linux, ru.unix"));
    CHECK(holds(copy, " * Origin: "));
    // And a MSGID of its own: two messages the network could not tell apart
    // would be one message to it.
    CHECK(fixture.kludgesIn("ru.linux", 1) != fixture.kludgesIn("ru.unix", 1));
}

TEST_CASE("A '#' on the echo being written in leaves 'Originally in' unsaid "
          "[copy][crosspost][compose]") {
    CopyFixture fixture;
    fixture.enter("ru.talk");

    fixture.begin({"XC: #ru.talk ru.linux"});
    fixture.storeAndProcess();

    CHECK(fixture.countIn("ru.linux") == 1);
    const auto text = fixture.textIn("ru.talk", 1);
    CHECK_FALSE(holds(text, "* Originally in"));
    CHECK(holds(text, "* Crossposted in ru.linux"));
}

TEST_CASE("XP: is XC: under another name [copy][crosspost][compose]") {
    CopyFixture fixture;
    fixture.enter("ru.talk");

    fixture.begin({"XP: ru.linux"});
    fixture.storeAndProcess();

    CHECK(fixture.countIn("ru.linux") == 1);
    CHECK(holds(fixture.textIn("ru.talk", 1), "* Crossposted in ru.linux"));
}

TEST_CASE("A mask that names no echo at all keeps its line [copy][crosspost]") {
    CopyFixture fixture;
    fixture.enter("ru.talk");

    fixture.begin({"XC: no.such.echo"});
    fixture.storeAndProcess();

    CHECK(holds(fixture.textIn("ru.talk", 1), "XC: no.such.echo"));
    CHECK(fixture.state.errorMessage.find("no.such.echo") != std::string::npos);
}

TEST_CASE("What the message keeps of its CC: line is compose_cc_list's "
          "[copy][compose][list]") {
    const auto textWith = [](CarbonList mode) {
        CopyFixture fixture;
        fixture.config.carbonList = mode;
        fixture.giveNodelist();
        fixture.enter("netmail");

        fixture.begin({"CC: Ivan Ivanov"});
        fixture.state.compose.toName = "Yegor Gluhov";
        fixture.state.compose.toAddr = "2:5020/1";
        fixture.storeAndProcess();
        REQUIRE(fixture.countIn("netmail") == 2);
        return std::make_pair(fixture.textIn("netmail", 1),
                              fixture.kludgesIn("netmail", 1));
    };

    const auto keep = textWith(CarbonList::Keep);
    CHECK(holds(keep.first, "CC: Ivan Ivanov"));

    const auto names = textWith(CarbonList::Names);
    CHECK(holds(names.first, "* Carbon copied to Ivan Ivanov"));
    CHECK_FALSE(holds(names.first, "CC: Ivan Ivanov"));

    const auto visible = textWith(CarbonList::Visible);
    CHECK(holds(visible.first, "* Carbon copied to Ivan Ivanov  2:5020/1234"));

    const auto remove = textWith(CarbonList::Remove);
    CHECK_FALSE(holds(remove.first, "Ivan Ivanov"));

    // The hidden form says the same thing to a program and nothing to a person:
    // a control line at the head of the message, and no text at all.
    const auto hidden = textWith(CarbonList::Hidden);
    CHECK_FALSE(holds(hidden.first, "Ivan Ivanov"));
    CHECK(holds(hidden.second, "@CC: Ivan Ivanov 2:5020/1234"));
}

TEST_CASE("What it keeps of its XC: line is compose_xc_list's "
          "[copy][crosspost][list]") {
    const auto textWith = [](CrosspostList mode) {
        CopyFixture fixture;
        fixture.config.crosspostList = mode;
        fixture.enter("ru.talk");
        fixture.begin({"XC: ru.linux"});
        fixture.storeAndProcess();
        // The crossposting happens whatever the list says about it.
        REQUIRE(fixture.countIn("ru.linux") == 1);
        return fixture.textIn("ru.talk", 1);
    };

    CHECK(holds(textWith(CrosspostList::Raw), "XC: ru.linux"));

    const auto verbose = textWith(CrosspostList::Verbose);
    CHECK(holds(verbose, "* Originally in ru.talk"));
    CHECK(holds(verbose, "* Crossposted in ru.linux"));
    CHECK_FALSE(holds(verbose, "XC: ru.linux"));

    const auto yes = textWith(CrosspostList::Yes);
    CHECK(holds(yes, "* Crossposted in ru.linux"));

    const auto none = textWith(CrosspostList::None);
    CHECK_FALSE(holds(none, "ru.linux"));
    CHECK_FALSE(holds(none, "Originally"));
}

TEST_CASE("A quoted command is not a command [copy][compose]") {
    CopyFixture fixture;
    fixture.giveNodelist();
    fixture.enter("netmail");

    fixture.begin({" IK> CC: Ivan Ivanov", "Not mine to send."});
    fixture.state.compose.toName = "Yegor Gluhov";
    fixture.state.compose.toAddr = "2:5020/1";
    CHECK_FALSE(fixture.store());

    REQUIRE(fixture.countIn("netmail") == 1);
    CHECK(holds(fixture.textIn("netmail", 1), " IK> CC: Ivan Ivanov"));
}
