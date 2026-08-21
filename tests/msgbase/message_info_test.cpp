#include <doctest/doctest.h>

#include <cstdint>
#include <string>

#include "config/text_util.hpp"
#include "domain/message.hpp"
#include "encoding/iconv_recoder.hpp"
#include "msgbase/ftn_msgbase.hpp"
#include "temp_msg_bases.hpp"
#include "temp_squish_base.hpp"
#include "test_paths.hpp"

using amberedit::config::text::startsWith;
using amberedit::domain::AreaConfig;
using amberedit::domain::MessageInfo;
using amberedit::domain::MessageInfoBlock;
using amberedit::domain::MessageInfoField;
using amberedit::domain::MsgBaseType;
using amberedit::encoding::isValidUtf8;
using amberedit::msgbase::FtnMsgBase;
using amberedit::test::TempJamBase;
using amberedit::test::TempSdmBase;
using amberedit::test::TempSquishBase;

namespace {

AreaConfig areaAt(const std::string& path, MsgBaseType type, const std::string& tag) {
    AreaConfig area;
    area.tag = tag;
    area.path = path;
    area.type = type;
    return area;
}

/// The block whose title begins with `prefix` — the dumps name their length in
/// their title, so a test that wants one asks for what it is rather than for
/// how big it happened to be.
const MessageInfoBlock* blockOf(const MessageInfo& info, const std::string& prefix) {
    for (const auto& block : info.blocks) {
        if (startsWith(block.title, prefix)) return &block;
    }
    return nullptr;
}

/// The first field with this label, wherever in the report it stands. Labels
/// repeat across blocks — a UMSGID is in the message header and in the index
/// record both — and the first is the message's own.
const MessageInfoField* fieldOf(const MessageInfo& info, const std::string& label) {
    for (const auto& block : info.blocks) {
        for (const auto& field : block.fields) {
            if (field.label == label) return &field;
        }
    }
    return nullptr;
}

std::string valueOf(const MessageInfo& info, const std::string& label) {
    const MessageInfoField* field = fieldOf(info, label);
    return field != nullptr ? field->value : std::string{};
}

}  // namespace

TEST_CASE("A Squish message says what the base holds about it [info][squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(areaAt(base.path(), MsgBaseType::Squish, "localnet")));
    REQUIRE(msgbase.count() > 1);

    const MessageInfo info = msgbase.info(1);
    CHECK_FALSE(info.empty());
    CHECK(info.title.find("Squish message 1 of") != std::string::npos);

    // The header fields come back as the message shows them: the report is
    // read beside the message, so a name in it has to be the same name.
    CHECK(valueOf(info, "From") == msgbase.header(1).from);
    CHECK(valueOf(info, "To") == msgbase.header(1).to);
    CHECK(valueOf(info, "Subject") == msgbase.header(1).subject);
    // The UMSGID is what a lastread mark is made of, and the base's own answer
    // has to agree with what the report reads off the frame.
    CHECK(valueOf(info, "Umsgid") == std::to_string(msgbase.uidOf(1)));
    CHECK(valueOf(info, "Msgbase").find("localnet.sqd") != std::string::npos);

    // Every record the message stands in, each under the heading GoldED+ gives
    // it — the reports are meant to be read side by side with its own.
    CHECK(blockOf(info, "Message Base Record:") != nullptr);
    CHECK(blockOf(info, "Message Index Record:") != nullptr);
    CHECK(blockOf(info, "Message Frame Record:") != nullptr);
    CHECK(valueOf(info, "Frame-ID") == "AFAE4453h");
    CHECK(valueOf(info, "MessageNumber")
              .find("(" + std::to_string(msgbase.uidOf(1)) + ")") != std::string::npos);

    const MessageInfoBlock* stored = blockOf(info, "Message header (XMSG)");
    REQUIRE(stored != nullptr);
    CHECK(stored->bytes.size() == 238);
    // The attribute word opens the XMSG, which is what says the dump is of the
    // record the fields above it describe.
    CHECK(valueOf(info, "Attr").find("h (") != std::string::npos);

    const MessageInfoBlock* text = blockOf(info, "Message text");
    REQUIRE(text != nullptr);
    CHECK_FALSE(text->bytes.empty());
    CHECK(text->title.find(std::to_string(text->bytes.size())) != std::string::npos);
}

TEST_CASE("An info report is only ever of a message that is there [info][squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(areaAt(base.path(), MsgBaseType::Squish, "localnet")));

    CHECK(msgbase.info(0).empty());
    CHECK(msgbase.info(msgbase.count() + 1).empty());

    // And there is nothing to report at all with no area open.
    FtnMsgBase closed;
    CHECK(closed.info(1).empty());
}

TEST_CASE("The text in an info report is converted like any other [info][squish]") {
    // The charsets fixture: "Привет" as the subject, in KOI8-R and in CP866.
    // Above the message-base port there are no single-byte charsets, and a
    // report is above it like everything else.
    const AreaConfig area =
        areaAt(amberedit::test::projectPath("testdata/msgbase/charsets"),
               MsgBaseType::Squish, "charsets");

    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(area));
    REQUIRE(msgbase.count() == 3);

    for (uint32_t number = 1; number <= 2; ++number) {
        INFO("message " << number);
        const MessageInfo info = msgbase.info(number);
        CHECK(valueOf(info, "Subject") == "Привет");
        CHECK(isValidUtf8(valueOf(info, "Subject")));
        // The numbers are ASCII whatever the message is written in, and are
        // handed on untouched.
        CHECK(valueOf(info, "UTC-Offset") == "0");
    }
}

TEST_CASE("A JAM message says what its subfields hold [info][jam]") {
    TempJamBase base;
    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(areaAt(base.path(), MsgBaseType::Jam, "area2")));
    REQUIRE(msgbase.count() == 1);

    const MessageInfo info = msgbase.info(1);
    CHECK(info.title.find("JAM message 1 of 1") != std::string::npos);
    CHECK(valueOf(info, "MessageNumber") == "1");
    CHECK(valueOf(info, "Signature") == "JAM");
    CHECK(blockOf(info, "Base Header:") != nullptr);
    CHECK(blockOf(info, "Index Record:") != nullptr);
    CHECK(valueOf(info, "BaseMsgNum") == "1");

    // JAM keeps the names and the subject as subfields rather than in the
    // header, so that is where the report shows them — one line each, under the
    // number and the name JAM-001 gives them.
    const MessageInfoBlock* subfields = blockOf(info, "Subfields:");
    REQUIRE(subfields != nullptr);
    bool sender = false;
    for (const auto& field : subfields->fields) {
        if (field.label.find("SENDERNAME") == std::string::npos) continue;
        sender = true;
        CHECK(field.label == "00002 SENDERNAME");
        CHECK(field.value == msgbase.header(1).from);
    }
    CHECK(sender);

    const MessageInfoBlock* stored = blockOf(info, "Message header (JAMHDR)");
    REQUIRE(stored != nullptr);
    CHECK(stored->bytes.size() == 76);
    CHECK(startsWith(stored->bytes, "JAM"));

    const MessageInfoBlock* text = blockOf(info, "Message text");
    REQUIRE(text != nullptr);
    CHECK(text->bytes.size() == std::stoul(valueOf(info, "TxtLen")));
}

TEST_CASE("A Fido *.msg message says which file it is [info][sdm]") {
    TempSdmBase base;
    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(areaAt(base.path(), MsgBaseType::Sdm, "netmail")));
    REQUIRE(msgbase.count() > 0);

    const MessageInfo info = msgbase.info(1);
    CHECK(info.title.find("Fido *.msg message 1 of") != std::string::npos);
    // The file is the message here, so its name — the number — is the first
    // thing worth saying about it.
    CHECK(valueOf(info, "File").find(valueOf(info, "Number") + ".msg") !=
          std::string::npos);
    CHECK(valueOf(info, "Number") == std::to_string(msgbase.uidOf(1)));
    CHECK(valueOf(info, "From") == msgbase.header(1).from);

    // The header's own fields, where the header has them: the date in words
    // after the subject, the count after it, and the four address words as the
    // net/node pairs they are — the zone and the point are in the kludges.
    CHECK(valueOf(info, "DateTime") == "13 Aug 26  10:15:20");
    CHECK(valueOf(info, "TimesRead") == "0");
    CHECK(valueOf(info, "OrigAddr") == "168/2");
    CHECK(valueOf(info, "DestAddr") == "168/1");

    // The eight bytes at 176 are the Opus stamps — the reading the driver takes
    // — and the report offers no second one, the FTS-0001 zone and point words
    // standing in the same place.
    CHECK(valueOf(info, "Written").find("2026-08-13 10:15:20") != std::string::npos);
    CHECK(valueOf(info, "Written").find("51EA5D0Dh") != std::string::npos);
    CHECK(valueOf(info, "Arrived").find("2026-08-13 10:15:20") != std::string::npos);
    CHECK(fieldOf(info, "DestZone") == nullptr);
    CHECK(fieldOf(info, "OrigPoint") == nullptr);

    const MessageInfoBlock* stored = blockOf(info, "Message header");
    REQUIRE(stored != nullptr);
    CHECK(stored->bytes.size() == 190);
}
