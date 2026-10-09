#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/area_manager.hpp"
#include "config/app_config.hpp"
#include "domain/message.hpp"
#include "ports/i_area_source.hpp"
#include "ports/i_lastread_store.hpp"
#include "temp_dir.hpp"
#include "test_strings.hpp"
#include "ui/app_state.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"

using amberedit::config::AppConfig;
using amberedit::domain::AreaConfig;
using amberedit::domain::AreaKind;
using amberedit::domain::FtnAddress;
using amberedit::domain::MessageDraft;
using amberedit::test::contains;
using amberedit::test::valueOf;
using amberedit::ui::AppState;

namespace attr = amberedit::domain::attr;
namespace message_list = amberedit::ui::screens::message_list;
namespace message_read = amberedit::ui::screens::message_read;

namespace {

class NoMarks final : public amberedit::ports::ILastReadStore {
public:
    uint32_t getLastRead(const AreaConfig&) override { return 0; }
    void setLastRead(const AreaConfig&, uint32_t) override {}
};

class OneArea final : public amberedit::ports::IAreaConfigSource {
public:
    explicit OneArea(AreaConfig area) : area_(std::move(area)) {}
    tl::expected<std::vector<AreaConfig>, amberedit::ErrorPtr> loadAreas() override {
        return std::vector<AreaConfig>{area_};
    }

private:
    AreaConfig area_;
};

AppConfig receiptConfig() {
    AppConfig cfg;
    cfg.userName = "Yegor Gluhov";
    cfg.userAddress = FtnAddress::parse("2:382/736");
    cfg.composeCharset = "CP866";
    cfg.origins = {"AmberEdit test"};
    return cfg;
}

/// One netmail area on a Squish base made where the area is first opened —
/// which is what an area a tosser config has just declared looks like.
struct NetmailFixture {
    explicit NetmailFixture(AppConfig cfg = receiptConfig())
        : config(std::move(cfg)),
          manager(std::make_unique<OneArea>(netmailArea()), std::make_unique<NoMarks>(),
                  config),
          state(manager, config) {
        static_cast<void>(manager.reload());
    }

    [[nodiscard]] AreaConfig netmailArea() const {
        AreaConfig area;
        area.tag = "netmail";
        area.path = dir.path("netmail");
        area.type = amberedit::domain::MsgBaseType::Squish;
        area.kind = AreaKind::Netmail;
        area.address = *FtnAddress::parse("2:382/736");
        return area;
    }

    /// Writes a netmail into the area and leaves it closed again, as it was.
    uint32_t put(const MessageDraft& draft) {
        const AreaConfig area = netmailArea();
        amberedit::ports::IMsgBase* base = valueOf(manager.openArea(area));
        REQUIRE(base != nullptr);
        const uint32_t number = valueOf(base->write(draft));
        manager.closeCurrentArea();
        manager.refreshArea(area);
        return number;
    }

    amberedit::test::TempDir dir;
    AppConfig config;
    amberedit::app::AreaManager manager;
    AppState state;
};

/// A netmail to the user, asking — or not — to be told it was read.
MessageDraft toUser(bool confirm) {
    MessageDraft draft;
    draft.from = "Vasya Pupkin";
    draft.to = "Yegor Gluhov";
    draft.subject = "are you there?";
    draft.origAddr = *FtnAddress::parse("2:5015/46");
    draft.destAddr = *FtnAddress::parse("2:382/736");
    draft.netmail = true;
    draft.attributes = attr::kPrivate;
    draft.charset = "CP866";
    draft.kludges = {"MSGID: 2:5015/46 68a1b2c3", "CHRS: CP866 2"};
    if (confirm) draft.kludges.emplace_back("FLAGS CFM");
    draft.lines = {"Hello?"};
    return draft;
}

/// The message the receipt is, read back out of the area.
struct Written {
    amberedit::domain::MessageHeader header;
    amberedit::domain::MessageBody body;
};
Written messageAt(NetmailFixture& fixture, uint32_t number) {
    amberedit::ports::IMsgBase* base =
        valueOf(fixture.manager.openArea(fixture.state.currentArea));
    REQUIRE(base != nullptr);
    Written out{base->header(number), base->body(number)};
    fixture.manager.closeCurrentArea();
    return out;
}

}  // namespace

TEST_CASE("A Cfm netmail is answered with a receipt into the same area "
          "[receipt][reader][squish]") {
    NetmailFixture fixture;
    REQUIRE(fixture.put(toUser(/*confirm=*/true)) == 1);

    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, fixture.netmailArea()).has_value());
    message_read::goToMessage(state, 1);
    REQUIRE(state.readHeader.has_value());
    CHECK(state.readHeader->wantsConfirmation());

    // The question comes with the message rather than with a keystroke, and Rcv
    // is on the netmail before it is answered either way.
    CHECK(state.confirm == AppState::Confirm::SendReceipt);
    CHECK(state.confirmChoice == AppState::ConfirmChoice::Yes);
    CHECK(state.readHeader->isRead());

    state.confirm = AppState::Confirm::None;
    message_read::sendReceipt(state);

    // Into the area the netmail is in, and the reader stays where it was.
    REQUIRE(state.messageCount == 2);
    CHECK(state.readHeader->number == 1);

    const Written receipt = messageAt(fixture, 2);
    CHECK(receipt.header.from == "Yegor Gluhov");
    CHECK(receipt.header.to == "Vasya Pupkin");
    CHECK(receipt.header.subject == "are you there?");
    CHECK(receipt.header.destAddr.toString() == "2:5015/46");
    CHECK(receipt.header.origAddr.toString() == "2:382/736");
    CHECK((receipt.header.attributes & attr::kLocal) != 0);
    CHECK((receipt.header.attributes & attr::kPrivate) != 0);
    // It is not itself a message asking to be told it was read.
    CHECK_FALSE(receipt.header.wantsConfirmation());

    // The text is the one line a config naming no cfm_template writes, and
    // nothing else at all: no tagline, no tearline, no origin. A receipt is an
    // acknowledgement rather than anything a person signed.
    std::vector<std::string> text;
    for (const auto& line : receipt.body.lines) {
        if (!line.kludge) text.push_back(line.text);
    }
    CHECK(text == std::vector<std::string>{"Confirmation Receipt"});
    CHECK(std::none_of(text.begin(), text.end(), [](const std::string& line) {
        return contains(line, "Origin:") || contains(line, "---");
    }));
    // And no line of it is flagged as a footer, the block not being there.
    CHECK(std::none_of(receipt.body.lines.begin(), receipt.body.lines.end(),
                       [](const auto& line) { return line.footer; }));
}

TEST_CASE("The receipt is asked about once per netmail [receipt][reader][squish]") {
    NetmailFixture fixture;
    REQUIRE(fixture.put(toUser(/*confirm=*/true)) == 1);

    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, fixture.netmailArea()).has_value());
    message_read::goToMessage(state, 1);
    REQUIRE(state.confirm == AppState::Confirm::SendReceipt);

    // Answered No, which stores nothing — and the netmail is not asked about
    // again, Rcv having been set when the question was put.
    state.confirm = AppState::Confirm::None;
    message_read::goToMessage(state, 1);
    CHECK(state.confirm == AppState::Confirm::None);
    CHECK(state.messageCount == 1);

    // Nor after the area has been left and entered again, the attribute being
    // on the message rather than in this session.
    message_list::leaveArea(state);
    REQUIRE(message_list::enterArea(state, fixture.netmailArea()).has_value());
    message_read::goToMessage(state, 1);
    CHECK(state.confirm == AppState::Confirm::None);
}

TEST_CASE("Nothing is asked about a netmail that is not the user's "
          "[receipt][reader][squish]") {
    NetmailFixture fixture;
    MessageDraft passing = toUser(/*confirm=*/true);
    // Somebody else's netmail, carried through this system: the request is made
    // of the node it was addressed to, which is not this one.
    passing.to = "Pyotr Sidorov";
    passing.destAddr = *FtnAddress::parse("2:5020/1");
    REQUIRE(fixture.put(passing) == 1);
    // And one of the user's own that asks for nothing.
    REQUIRE(fixture.put(toUser(/*confirm=*/false)) == 2);

    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, fixture.netmailArea()).has_value());
    message_read::goToMessage(state, 1);
    CHECK(state.confirm == AppState::Confirm::None);
    CHECK_FALSE(state.readHeader->isRead());

    message_read::goToMessage(state, 2);
    CHECK(state.confirm == AppState::Confirm::None);
    CHECK_FALSE(state.readHeader->isRead());
    CHECK(state.messageCount == 2);
}

TEST_CASE("A Cfm message in an echo is let alone [receipt][reader][squish]") {
    // Cfm is a request made of the node a message was addressed to, and an echo
    // addresses nobody — so an echomail carrying the word is nothing to answer.
    AppConfig cfg = receiptConfig();
    NetmailFixture fixture(cfg);
    AreaConfig echo = fixture.netmailArea();
    echo.kind = AreaKind::Echo;
    echo.tag = "ru.linux";

    amberedit::ports::IMsgBase* base = valueOf(fixture.manager.openArea(echo));
    REQUIRE(base != nullptr);
    MessageDraft draft = toUser(/*confirm=*/true);
    draft.netmail = false;
    REQUIRE(valueOf(base->write(draft)) == 1);
    fixture.manager.closeCurrentArea();

    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, echo).has_value());
    message_read::goToMessage(state, 1);
    REQUIRE(state.readHeader.has_value());
    // Read and shown, which is the whole of what becomes of it.
    CHECK(state.readHeader->wantsConfirmation());
    CHECK(state.confirm == AppState::Confirm::None);
    CHECK(state.messageCount == 1);
}

TEST_CASE("cfm_attributes is what the receipt carries [receipt][reader][squish]") {
    namespace attr = amberedit::domain::attr;

    AppConfig cfg = receiptConfig();
    // Not what a netmail being typed would start with: K/s so that the tosser
    // takes it away once it has gone, and not private.
    cfg.cfmAttributes = attr::kLocal | attr::kKillSent;
    NetmailFixture fixture(cfg);
    REQUIRE(fixture.put(toUser(/*confirm=*/true)) == 1);

    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, fixture.netmailArea()).has_value());
    message_read::goToMessage(state, 1);
    REQUIRE(state.confirm == AppState::Confirm::SendReceipt);
    state.confirm = AppState::Confirm::None;
    message_read::sendReceipt(state);
    REQUIRE(state.messageCount == 2);

    const Written receipt = messageAt(fixture, 2);
    CHECK(amberedit::domain::messageAttributes(receipt.header.attributes) ==
          std::vector<std::string>{"Uns", "K/s", "Loc"});
}

TEST_CASE("cfm_template is what the receipt says [receipt][reader][squish]") {
    const auto path = std::filesystem::temp_directory_path() / "amberedit_cfm_ui.tpl";
    {
        std::ofstream out(path);
        out << "@Quoted@oname, your netmail was read.\n";
    }

    AppConfig cfg = receiptConfig();
    cfg.cfmTemplatePath = path.string();
    NetmailFixture fixture(cfg);
    REQUIRE(fixture.put(toUser(/*confirm=*/true)) == 1);

    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, fixture.netmailArea()).has_value());
    message_read::goToMessage(state, 1);
    REQUIRE(state.confirm == AppState::Confirm::SendReceipt);
    state.confirm = AppState::Confirm::None;
    message_read::sendReceipt(state);
    REQUIRE(state.messageCount == 2);

    const Written receipt = messageAt(fixture, 2);
    std::vector<std::string> text;
    for (const auto& line : receipt.body.lines) {
        if (!line.kludge) text.push_back(line.text);
    }
    REQUIRE_FALSE(text.empty());
    CHECK(text.front() == "Vasya Pupkin, your netmail was read.");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}
