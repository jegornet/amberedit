#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "config/app_config.hpp"
#include "domain/ftn_address.hpp"
#include "nodelist/nodelist_source.hpp"
#include "temp_dir.hpp"
#include "test_paths.hpp"
#include "test_strings.hpp"
#include "ui/setup/file_picker.hpp"
#include "ui/setup/setup_wizard.hpp"
#include "ui/setup/wizard_checks.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"

using amberedit::config::AppConfig;
using amberedit::config::TosserConfigFormat;
using amberedit::domain::FtnAddress;
using amberedit::test::contains;
using amberedit::test::errorOf;
using amberedit::test::projectPath;
using amberedit::test::TempDir;
using amberedit::test::valueOf;
using amberedit::ui::term::Event;

namespace setup = amberedit::ui::setup;
namespace term = amberedit::ui::term;

namespace {

FtnAddress addressOf(const std::string& text) {
    const auto parsed = FtnAddress::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

/// The wizard with a window to draw in, as `runSetup` hands it one.
setup::SetupState wizard() {
    setup::SetupState state;
    setup::begin(state, projectPath("build/bin/amberedit"));
    state.width = 100;
    state.height = 30;
    return state;
}

void type(setup::SetupState& state, const std::string& word) {
    for (char c : word) setup::handleEvent(state, Event::Character(c));
}

/// Next, pressed: Enter walks the questions of a step, so the button is the one
/// thing that says the step is answered.
setup::Outcome pressNext(setup::SetupState& state) {
    while (state.stop != setup::Stop::Next) setup::handleEvent(state, Event::Tab);
    return setup::handleEvent(state, Event::Return);
}

/// Everything the screen says, one string per row, as a reader would see it.
std::vector<std::string> screenRows(setup::SetupState& state) {
    term::Screen screen(state.width, state.height);
    term::render(screen, setup::render(state));

    std::vector<std::string> rows;
    for (int y = 0; y < state.height; ++y) {
        std::string row;
        for (int x = 0; x < state.width; ++x) row += screen.at(x, y).glyph;
        rows.push_back(row);
    }
    return rows;
}

bool saysSomewhere(setup::SetupState& state, const std::string& text) {
    for (const auto& row : screenRows(state)) {
        if (contains(row, text)) return true;
    }
    return false;
}

Event clickAt(int x, int y) {
    term::MouseEvent mouse;
    mouse.button = term::MouseEvent::Button::Left;
    mouse.motion = term::MouseEvent::Motion::Pressed;
    mouse.x = x;
    mouse.y = y;
    return Event::Mouse(mouse);
}

}  // namespace

TEST_CASE("the charset guessed from an address [setup]") {
    // The ex-USSR of zone 2, where a message with no CHRS is CP866 and always
    // was.
    CHECK(setup::defaultReadCharset(addressOf("2:5020/9999")) == "CP866");
    CHECK(setup::defaultReadCharset(addressOf("2:5015/46")) == "CP866");
    CHECK(setup::defaultReadCharset(addressOf("2:6000/9999")) == "CP866");
    // The rest of zone 2 is western Europe.
    CHECK(setup::defaultReadCharset(addressOf("2:382/736")) == "LATIN-1");
    CHECK(setup::defaultReadCharset(addressOf("2:5/1")) == "LATIN-1");
    // And everywhere else is the DOS codepage the mail was written on.
    CHECK(setup::defaultReadCharset(addressOf("1:234/56")) == "CP437");
    CHECK(setup::defaultReadCharset(addressOf("3:770/1")) == "CP437");
}

TEST_CASE("an address is checked before it is written [setup]") {
    CHECK(setup::checkAddress("2:382/736.1").has_value());
    CHECK(setup::checkAddress("  2:382/736  ").has_value());
    CHECK_FALSE(setup::checkAddress("2:382").has_value());
    CHECK_FALSE(setup::checkAddress("382/736").has_value());
    CHECK_FALSE(setup::checkAddress("").has_value());
    CHECK_FALSE(setup::checkAddress("0:0/0").has_value());
}

TEST_CASE("a charset iconv does not know is refused [setup]") {
    CHECK(setup::checkCharsetAnswer("UTF-8").has_value());
    CHECK(setup::checkCharsetAnswer("CP866").has_value());
    CHECK_FALSE(setup::checkCharsetAnswer("").has_value());
    CHECK_FALSE(setup::checkCharsetAnswer("NO-SUCH-CHARSET").has_value());
    // One word, because the config reads it as one value.
    CHECK_FALSE(setup::checkCharsetAnswer("IBMPC 2").has_value());
}

TEST_CASE("a name has to be one, and one the config can carry [setup]") {
    CHECK(setup::checkName("John Doe").has_value());
    CHECK_FALSE(setup::checkName("   ").has_value());
    CHECK_FALSE(setup::checkName("John \"Doe\"").has_value());
}

TEST_CASE("the tosser config is read the way AmberEdit will read it [setup]") {
    const std::string fidoconfig = projectPath("testdata/tossers/areas");
    const std::string areasBbs = projectPath("testdata/tossers/areas.bbs");
    const std::string squish = projectPath("testdata/tossers/squish.cfg");

    CHECK(valueOf(setup::checkTosserConfig(fidoconfig, TosserConfigFormat::Fidoconfig)) >
          0);
    CHECK(valueOf(setup::checkTosserConfig(areasBbs, TosserConfigFormat::AreasBbs)) > 0);
    CHECK(valueOf(setup::checkTosserConfig(squish, TosserConfigFormat::SquishCfg)) > 0);

    // The wrong format over the right file reads no areas at all, which is the
    // mistake this step is for.
    const auto wrong = setup::checkTosserConfig(areasBbs, TosserConfigFormat::SquishCfg);
    REQUIRE_FALSE(wrong.has_value());
    CHECK_MESSAGE(contains(wrong.error(), "no areas"), wrong.error());

    // And a directory, or nothing at all, is said so plainly.
    const TempDir dir;
    CHECK_FALSE(
        setup::checkTosserConfig(dir.path(""), TosserConfigFormat::AreasBbs).has_value());
    CHECK_FALSE(
        setup::checkTosserConfig(dir.path("nothing"), TosserConfigFormat::Fidoconfig)
            .has_value());
}

TEST_CASE("the picker shows the file the format keeps its config in [setup]") {
    CHECK(setup::acceptsTosserFile(TosserConfigFormat::AreasBbs, "areas.bbs"));
    CHECK(setup::acceptsTosserFile(TosserConfigFormat::AreasBbs, "AREAS.BBS"));
    CHECK_FALSE(setup::acceptsTosserFile(TosserConfigFormat::AreasBbs, "areas.bbs.bak"));
    CHECK_FALSE(setup::acceptsTosserFile(TosserConfigFormat::AreasBbs, "squish.cfg"));
    CHECK(setup::acceptsTosserFile(TosserConfigFormat::SquishCfg, "Squish.cfg"));
    // A fidoconfig is called whatever the sysop called it.
    CHECK(setup::acceptsTosserFile(TosserConfigFormat::Fidoconfig, "config"));
    CHECK(setup::acceptsTosserFile(TosserConfigFormat::Fidoconfig, "areas"));
}

TEST_CASE("the compiled nodelist goes beside the nodelist [setup]") {
    CHECK(setup::defaultNodelistDb("/home/ftn/nodelist/z2daily.999") ==
          "/home/ftn/nodelist/amberndl.db");
    CHECK(setup::defaultNodelistDb("").empty());
}

TEST_CASE("the template is found where an install would have put it [setup]") {
    const TempDir dir;
    const std::string program = dir.path("amberedit");
    {
        std::ofstream(dir.path("default.tpl")) << "@name\n";
    }
    const auto found = setup::probeTemplate(program);
    REQUIRE(found.has_value());
    CHECK(contains(*found, "default.tpl"));
    // Absolute, or a config naming it would read it against whatever directory
    // AmberEdit is next started in.
    CHECK(std::filesystem::path(*found).is_absolute());
}

TEST_CASE("no template anywhere leaves one beside the config [setup]") {
    const TempDir dir;
    // A program path in an empty directory, so nothing but the working
    // directory and the two prefixes can answer — and none of the three is
    // likely to hold one when the tests run.
    const std::string program = dir.path("bin/amberedit");
    if (setup::probeTemplate(program)) return;  // an AmberEdit is installed here

    const std::string config = dir.path("amberedit.cfg");
    const std::string written = valueOf(setup::ensureTemplate(config, program));
    CHECK(contains(written, "default.tpl"));
    CHECK(std::filesystem::exists(written));
    // And it is the template that ships, not an empty file.
    CHECK(std::filesystem::file_size(written) > 0);
}

TEST_CASE("where the config may be written [setup]") {
    const TempDir dir;
    CHECK(setup::checkTargetPath(dir.path("amberedit.cfg")).has_value());
    CHECK_FALSE(setup::checkTargetPath(dir.path("")).has_value());  // a directory
    CHECK_FALSE(setup::checkTargetPath(dir.path("nowhere/amberedit.cfg")).has_value());
    CHECK_FALSE(setup::checkTargetPath("").has_value());

    std::ofstream(dir.path("taken.cfg")) << "x\n";
    CHECK_FALSE(setup::checkTargetPath(dir.path("taken.cfg")).has_value());
}

TEST_CASE("a home directory is written back as a tilde [setup]") {
    const char* home = std::getenv("HOME");
    if (home == nullptr) return;
    CHECK(setup::abbreviateHome(std::string(home) + "/ftn/etc/areas") ==
          "~/ftn/etc/areas");
    CHECK(setup::abbreviateHome("/etc/areas") == "/etc/areas");
    // The home directory itself is not under itself, so it stays as it is.
    CHECK(setup::abbreviateHome(home) == std::string(home));
}

TEST_CASE("the picker leaves out what the format will not have [setup]") {
    const TempDir dir;
    std::filesystem::create_directory(dir.path("etc"));
    std::ofstream(dir.path("areas.bbs")) << "\n";
    std::ofstream(dir.path("squish.cfg")) << "\n";

    setup::FilePicker picker;
    picker.accepts = [](std::string_view name) {
        return setup::acceptsTosserFile(TosserConfigFormat::AreasBbs, name);
    };
    setup::open(picker, dir.path(""));

    std::vector<std::string> names;
    for (const auto& entry : picker.entries) names.push_back(entry.name);
    CHECK(std::find(names.begin(), names.end(), "areas.bbs") != names.end());
    CHECK(std::find(names.begin(), names.end(), "squish.cfg") == names.end());
    // A directory is always shown: a file is picked by walking to it.
    CHECK(std::find(names.begin(), names.end(), "etc") != names.end());
    // And `..` is the first row, as it is in every listing AmberEdit draws.
    REQUIRE_FALSE(picker.entries.empty());
    CHECK(picker.entries.front().name == "..");
}

TEST_CASE("the wizard asks the five questions in order [setup]") {
    setup::SetupState state = wizard();

    CHECK(saysSomewhere(state, "General parameters — step 1 of 6"));
    type(state, "John Doe");
    setup::handleEvent(state, Event::Tab);
    type(state, "2:382/736");
    // Next with the two answered goes on to the file, and the address has
    // guessed the charset on the way.
    pressNext(state);
    CHECK(state.step == setup::Step::TosserFile);
    CHECK(state.readCharset.value == "LATIN-1");
    CHECK(saysSomewhere(state, "Tosser config file — step 2 of 6"));

    state.tosserConfigPath = projectPath("testdata/tossers/areas");
    pressNext(state);
    REQUIRE_MESSAGE(state.step == setup::Step::ReadCharset, state.error);
    CHECK(saysSomewhere(state, "Incoming charset — step 3 of 6"));
    CHECK(saysSomewhere(state, "e.g. CP866 or CP437 or LATIN-1"));

    pressNext(state);
    REQUIRE(state.step == setup::Step::ComposeCharset);
    // The charset written follows the one read until somebody says otherwise.
    CHECK(state.composeCharset.value == "LATIN-1");
    CHECK(saysSomewhere(state, "Outgoing charset — step 4 of 6"));
    CHECK(saysSomewhere(state, "e.g. UTF-8 or CP866 or CP437 or LATIN-1"));

    pressNext(state);
    REQUIRE(state.step == setup::Step::Nodelist);
    CHECK(saysSomewhere(state, "Nodelist file — step 5 of 6"));
    CHECK(saysSomewhere(state, "ZIP"));

    // The nodelist may be skipped, which is what the button on that step is for.
    while (state.stop != setup::Stop::Skip) setup::handleEvent(state, Event::Tab);
    setup::handleEvent(state, Event::Return);
    CHECK(state.step == setup::Step::Summary);
    // The last step is the file itself, numbered like the rest.
    CHECK(saysSomewhere(state, "The config to write — step 6 of 6"));
    CHECK(state.nodelistPath.empty());
}

TEST_CASE("Enter walks the questions and complains about none of them [setup]") {
    setup::SetupState state = wizard();
    type(state, "John Doe");

    // Nothing is in the address field yet, and that is not a mistake — it is a
    // step being answered. Enter says so by moving on to it.
    setup::handleEvent(state, Event::Return);
    CHECK(state.stop == setup::Stop::Address);
    CHECK(state.error.empty());
    CHECK(state.step == setup::Step::Identity);

    // The radio next, and then Next: the questions in the order they are drawn,
    // and the button once they have all been asked.
    setup::handleEvent(state, Event::Return);
    CHECK(state.stop == setup::Stop::Format);
    setup::handleEvent(state, Event::Return);
    CHECK(state.stop == setup::Stop::Next);
    CHECK(state.error.empty());
    CHECK(state.step == setup::Step::Identity);

    // And there it checks, the step being said to be done.
    setup::handleEvent(state, Event::Return);
    CHECK(state.step == setup::Step::Identity);
    CHECK_MESSAGE(contains(state.error, "address"), state.error);
}

TEST_CASE("a bad answer stops the step and says why [setup]") {
    setup::SetupState state = wizard();
    type(state, "John Doe");
    setup::handleEvent(state, Event::Tab);
    type(state, "382/736");

    pressNext(state);
    CHECK(state.step == setup::Step::Identity);
    CHECK_MESSAGE(contains(state.error, "not an FTN address"), state.error);
    CHECK(state.stop == setup::Stop::Address);
    CHECK(saysSomewhere(state, "not an FTN address"));

    // And the next keystroke stops it being shouted at.
    setup::handleEvent(state, Event::Character('2'));
    CHECK(state.error.empty());
}

TEST_CASE("a guess the user has typed over is not made again [setup]") {
    setup::SetupState state = wizard();
    type(state, "John Doe");
    setup::handleEvent(state, Event::Tab);
    type(state, "2:5020/9999");
    pressNext(state);
    REQUIRE(state.step == setup::Step::TosserFile);
    CHECK(state.readCharset.value == "CP866");

    // Back to the address, and a charset the user has since chosen themselves.
    amberedit::ui::setFieldValue(state.readCharset, "KOI8-R");
    state.readCharset.touched = true;
    while (state.stop != setup::Stop::Back) setup::handleEvent(state, Event::Tab);
    setup::handleEvent(state, Event::Return);
    REQUIRE(state.step == setup::Step::Identity);
    pressNext(state);
    CHECK(state.readCharset.value == "KOI8-R");
}

TEST_CASE("Esc takes two presses, having written nothing [setup]") {
    setup::SetupState state = wizard();
    CHECK(setup::handleEvent(state, Event::Escape) == setup::Outcome::Ignored);
    CHECK(contains(state.error, "Esc again"));
    CHECK(setup::handleEvent(state, Event::Escape) == setup::Outcome::Cancelled);
}

TEST_CASE("the wizard writes the config it showed [setup][slow]") {
    const TempDir dir;
    setup::SetupState state = wizard();

    type(state, "John Doe");
    setup::handleEvent(state, Event::Tab);
    type(state, "2:382/736.1");
    pressNext(state);

    state.tosserConfigPath = projectPath("testdata/tossers/areas.bbs");
    state.format = amberedit::config::TosserConfigFormat::AreasBbs;
    pressNext(state);
    REQUIRE_MESSAGE(state.step == setup::Step::ReadCharset, state.error);
    pressNext(state);
    pressNext(state);
    REQUIRE(state.step == setup::Step::Nodelist);

    while (state.stop != setup::Stop::Skip) setup::handleEvent(state, Event::Tab);
    setup::handleEvent(state, Event::Return);
    REQUIRE(state.step == setup::Step::Summary);

    // The summary says what was answered, and where the config goes is a field
    // like any other.
    CHECK(saysSomewhere(state, "John Doe"));
    CHECK(saysSomewhere(state, "areas.bbs"));
    amberedit::ui::setFieldValue(state.target, dir.path("amberedit.cfg"));

    REQUIRE_MESSAGE(pressNext(state) == setup::Outcome::Saved, state.error);

    const AppConfig written = valueOf(AppConfig::loadFromFile(state.savedPath));
    CHECK(written.userName == "John Doe");
    REQUIRE(written.userAddress.has_value());
    CHECK(written.userAddress->toString() == "2:382/736.1");
    CHECK(written.tosserConfigFormat == TosserConfigFormat::AreasBbs);
    CHECK(written.defaultCharset == "LATIN-1");
    CHECK(written.nodelistSources.empty());
}

TEST_CASE("a nodelist is written as the pattern it is one of [setup]") {
    const TempDir dir;
    {
        std::ofstream(dir.path("z2daily.255")) << "x\n";
    }

    setup::SetupState state = wizard();
    state.step = setup::Step::Nodelist;
    state.stop = setup::Stop::Picker;
    setup::open(state.picker, dir.path(""));

    // Onto the file and Enter, which is how a nodelist is picked.
    while (state.picker.entries[static_cast<size_t>(state.picker.cursor)].name !=
           "z2daily.255") {
        setup::handleEvent(state, Event::ArrowDown);
    }
    setup::handleEvent(state, Event::Return);

    // Today's nodelist written as the pattern that will still be a nodelist
    // tomorrow, and the compiled file beside it.
    CHECK(contains(state.nodelistPath, "z2daily.999"));
    CHECK(contains(state.nodelistDb.value, "amberndl.db"));
    CHECK(state.stop == setup::Stop::NodelistDb);
}

TEST_CASE("the wizard says so in a window it does not fit in [setup]") {
    setup::SetupState state = wizard();
    state.width = 30;
    state.height = 8;
    CHECK(saysSomewhere(state, "needs"));
}

TEST_CASE("a click puts the typing where it landed [setup]") {
    setup::SetupState state = wizard();
    // Drawn once, so that the boxes know where they are.
    screenRows(state);
    REQUIRE_FALSE(state.address.box.IsEmpty());
    setup::handleEvent(state,
                       clickAt(state.address.box.x_min + 1, state.address.box.y_min));
    CHECK(state.stop == setup::Stop::Address);
}

TEST_CASE("a file the listing has lit is the answer, clicked or arrowed to [setup]") {
    const TempDir dir;
    {
        std::ofstream(dir.path("areas.bbs")) << "/msg/localnet localnet 2:382/736\n";
    }

    setup::SetupState state = wizard();
    type(state, "John Doe");
    setup::handleEvent(state, Event::Tab);
    type(state, "2:382/736");
    // The areas.bbs radio, so the listing shows the file that was just written.
    setup::handleEvent(state, Event::Tab);
    setup::handleEvent(state, Event::ArrowDown);
    REQUIRE(state.format == TosserConfigFormat::AreasBbs);
    pressNext(state);
    REQUIRE(state.step == setup::Step::TosserFile);
    setup::open(state.picker, dir.path(""));
    state.stop = setup::Stop::Picker;

    SUBCASE("clicked once") {
        // Drawn, so that the rows know where they are.
        screenRows(state);
        REQUIRE(state.picker.entries.size() == 2);
        REQUIRE(state.picker.rowBoxes.size() >= 2);
        const auto& row = state.picker.rowBoxes[1];
        REQUIRE(state.picker.entries[static_cast<size_t>(row.index)].name == "areas.bbs");
        setup::handleEvent(state, clickAt(row.box.x_min + 1, row.box.y_min));
        // One click lights the row and does not open it; Next takes what is lit.
        CHECK(state.step == setup::Step::TosserFile);
    }
    SUBCASE("arrowed to") {
        setup::handleEvent(state, Event::ArrowDown);
    }

    pressNext(state);
    REQUIRE_MESSAGE(state.step == setup::Step::ReadCharset, state.error);
    CHECK(contains(state.tosserConfigPath, "areas.bbs"));
}

TEST_CASE("a lit nodelist is the answer too, and is generalized [setup]") {
    const TempDir dir;
    {
        std::ofstream(dir.path("z2daily.255")) << "x\n";
    }

    setup::SetupState state = wizard();
    state.step = setup::Step::Nodelist;
    state.stop = setup::Stop::Picker;
    setup::open(state.picker, dir.path(""));
    while (state.picker.entries[static_cast<size_t>(state.picker.cursor)].name !=
           "z2daily.255") {
        setup::handleEvent(state, Event::ArrowDown);
    }

    // Never picked with Enter — only lit — and Next takes it all the same.
    REQUIRE(state.nodelistPath.empty());
    pressNext(state);
    CHECK(state.step == setup::Step::Summary);
    CHECK(contains(state.nodelistPath, "z2daily.999"));
    CHECK(contains(state.nodelistDb.value, "amberndl.db"));
}

TEST_CASE("a listing nobody has touched is not an answer [setup]") {
    const TempDir dir;
    {
        std::ofstream(dir.path("areas")) << "EchoArea x /msg/x\n";
    }

    setup::SetupState state = wizard();
    state.step = setup::Step::TosserFile;
    state.stop = setup::Stop::Picker;
    setup::open(state.picker, dir.path(""));
    // The cursor starts on `..`, which is a directory and says nothing.
    pressNext(state);
    CHECK(state.step == setup::Step::TosserFile);
    CHECK_MESSAGE(contains(state.error, "pick"), state.error);
}
