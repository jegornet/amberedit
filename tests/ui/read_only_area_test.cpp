#include <doctest/doctest.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <filesystem>
#include <string>

#include <memory>
#include <utility>
#include <vector>

#include "app/area_manager.hpp"
#include "app/navigator.hpp"
#include "config/app_config.hpp"
#include "msgbase/null_lastread_store.hpp"
#include "ports/i_area_source.hpp"
#include "domain/ftn_address.hpp"
#include "temp_squish_base.hpp"
#include "test_strings.hpp"
#include "ui/area_fixture.hpp"
#include "ui/screens/compose_screen.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"

using amberedit::app::ScreenId;
using amberedit::config::AppConfig;
using amberedit::test::AreaFixture;
using amberedit::test::TempSquishBase;
using amberedit::test::contains;

namespace compose = amberedit::ui::screens::compose;
namespace message_list = amberedit::ui::screens::message_list;
namespace message_read = amberedit::ui::screens::message_read;
namespace fs = std::filesystem;

namespace {

/// Whether taking the write bits away really takes writing away here: root is
/// refused nothing, and Windows has no such bits to take.
bool readOnlyBites() {
#ifdef _WIN32
    return false;
#else
    return ::geteuid() != 0;
#endif
}

void setWritable(const std::string& path, bool writable) {
    const fs::perms bits =
        fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
    for (const char* extension : {".sqd", ".sqi"}) {
        std::error_code ec;
        fs::permissions(path + extension, writable ? fs::perms::owner_write : bits,
                        writable ? fs::perm_options::add : fs::perm_options::remove, ec);
    }
}

/// A config a message could be written under, so that nothing but the base
/// stands between the user and the editor: without a sender's name and address
/// the header block keeps hold of the message on its own account.
/// The areas a manager is to find, for the one test that needs two of them.
class ListedAreas final : public amberedit::ports::IAreaConfigSource {
public:
    explicit ListedAreas(std::vector<amberedit::domain::AreaConfig> areas)
        : areas_(std::move(areas)) {}
    tl::expected<std::vector<amberedit::domain::AreaConfig>, amberedit::ErrorPtr>
    loadAreas() override {
        return areas_;
    }

private:
    std::vector<amberedit::domain::AreaConfig> areas_;
};

amberedit::domain::AreaConfig squishAreaAt(const std::string& tag,
                                           const std::string& path) {
    amberedit::domain::AreaConfig area;
    area.tag = tag;
    area.path = path;
    area.type = amberedit::domain::MsgBaseType::Squish;
    area.kind = amberedit::domain::AreaKind::Echo;
    return area;
}

AppConfig writableConfig() {
    AppConfig cfg;
    cfg.userName = "Yegor Gluhov";
    cfg.userAddress = amberedit::domain::FtnAddress::parse("2:382/736");
    return cfg;
}

}  // namespace

TEST_CASE("The editor does not open on an area that cannot be written "
          "[readonly][compose][squish]") {
    if (!readOnlyBites()) {
        MESSAGE("mode bits do not bite here: root, or Windows");
        return;
    }

    TempSquishBase base;
    // Read-only before the area is ever entered, which is how the user meets
    // one: the spool is somebody else's and always was.
    setWritable(base.path(), false);

    AreaFixture fixture(base.path(), writableConfig());
    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, fixture.area).has_value());
    message_read::goToMessage(state, 1);
    REQUIRE(state.readHeader.has_value());
    // The area is read exactly as any other is. That is the half that must go
    // on working.
    REQUIRE(state.messageCount > 0);

    const ScreenId reading = state.navigator.current();

    const auto checkRefused = [&](const char* what) {
        CHECK_MESSAGE(state.navigator.current() == reading, what);
        CHECK_MESSAGE(state.navigator.current() != ScreenId::Compose, what);
        // And says so, naming the area: a key that does nothing at all is the
        // bug this stands in place of.
        CHECK_MESSAGE(!state.errorMessage.empty(), what);
        CHECK_MESSAGE(contains(state.errorMessage, fixture.area.tag),
                      state.errorMessage);
        // Over the reader, not instead of it: acknowledging leaves the user on
        // the message they were reading.
        CHECK_MESSAGE(!state.errorEndsScreen, what);
        state.errorMessage.clear();
        state.errorEndsScreen = true;
    };

    compose::startReply(state);
    checkRefused("reply");

    compose::startCommentReply(state);
    checkRefused("comment reply");

    compose::startNew(state);
    checkRefused("new message");

    REQUIRE(state.readBody.has_value());
    compose::startChange(state, /*notice=*/false);
    checkRefused("change");

    setWritable(base.path(), true);
}

TEST_CASE("The editor opens on the same area once it can be written "
          "[readonly][compose][squish]") {
    // The other half of the pair: the refusal is about the base's mode and
    // nothing else about the area, so the same area with its write bits back
    // opens the editor as it always did.
    TempSquishBase base;
    AreaFixture fixture(base.path(), writableConfig());
    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, fixture.area).has_value());
    message_read::goToMessage(state, 1);
    REQUIRE(state.readHeader.has_value());

    compose::startReply(state);
    CHECK(state.navigator.current() == ScreenId::Compose);
    CHECK(state.errorMessage.empty());
}

TEST_CASE("A base that refuses the message says so over the editor that still "
          "holds it [readonly][compose][squish]") {
    // The other half of the pair, and the one the check at the door cannot
    // answer: the message is going into an area that is not open, so whether it
    // can be written is only known once it has been opened for it. Nothing must
    // be lost, and the keystroke must not pass in silence.
    if (!readOnlyBites()) {
        MESSAGE("mode bits do not bite here: root, or Windows");
        return;
    }

    TempSquishBase here;
    TempSquishBase there;
    setWritable(there.path(), false);

    const auto source = squishAreaAt("here.echo", here.path());
    const auto target = squishAreaAt("there.echo", there.path());
    AppConfig config = writableConfig();
    amberedit::app::AreaManager manager(
        std::make_unique<ListedAreas>(std::vector{source, target}),
        std::make_unique<amberedit::msgbase::NullLastReadStore>(), config);
    REQUIRE(manager.reload().has_value());
    amberedit::ui::AppState state(manager, config);

    REQUIRE(message_list::enterArea(state, source).has_value());
    message_read::goToMessage(state, 1);
    REQUIRE(state.readHeader.has_value());

    // The area on screen can be written, so the editor opens: this is the case
    // the door lets through.
    compose::startReplyElsewhere(state, target);
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    REQUIRE(state.errorMessage.empty());

    state.compose.subject = "answer";
    compose::saveMessage(state);

    // Still in the editor, with the message in it — and told why.
    CHECK(state.navigator.current() == ScreenId::Compose);
    CHECK(state.compose.subject == "answer");
    CHECK_FALSE(state.errorMessage.empty());
    CHECK_FALSE(state.errorEndsScreen);

    setWritable(there.path(), true);
}
