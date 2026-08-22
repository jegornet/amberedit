#include <doctest/doctest.h>

#include <string>

#include "app/compose_prefill.hpp"

using amberedit::app::ComposeFields;
using amberedit::config::AppConfig;
using amberedit::domain::AreaConfig;
using amberedit::domain::AreaKind;
using amberedit::domain::FtnAddress;
using amberedit::domain::MessageHeader;

namespace {

/// A config with a name, a main address and one AKA claiming zone 192 —
/// enough for every rule the four cases are made of.
AppConfig editorConfig() {
    AppConfig config;
    config.userName = "Yegor Gluhov";
    config.userAddress = FtnAddress::parse("2:5020/9999.1");

    amberedit::config::AkaMatch aka;
    aka.aka = *FtnAddress::parse("192:168/2");
    aka.patterns.push_back(*amberedit::domain::AddressPattern::parse("192:*"));
    config.akaMatches.push_back(aka);
    return config;
}

/// An area of the given kind, presented under `address` — or under none, which
/// is what a tosser config that states no AKA leaves behind.
AreaConfig areaOf(AreaKind kind, const std::string& address) {
    AreaConfig area;
    area.tag = kind == AreaKind::Netmail ? "NETMAIL" : "ru.linux";
    area.kind = kind;
    if (!address.empty()) area.address = *FtnAddress::parse(address);
    return area;
}

MessageHeader messageFrom(const std::string& name, const std::string& origAddr,
                          const std::string& destAddr) {
    MessageHeader header;
    header.from = name;
    header.to = "Yegor Gluhov";
    header.subject = "test";
    if (!origAddr.empty()) header.origAddr = *FtnAddress::parse(origAddr);
    if (!destAddr.empty()) header.destAddr = *FtnAddress::parse(destAddr);
    return header;
}

}  // namespace

TEST_CASE("A new netmail starts from the area's AKA [compose]") {
    const auto fields = amberedit::app::newMessage(
        editorConfig(), areaOf(AreaKind::Netmail, "192:168/2"));

    CHECK(fields.netmail);
    CHECK_FALSE(fields.reply);
    CHECK(fields.fromName == "Yegor Gluhov");
    CHECK(fields.fromAddr == "192:168/2");
    // Who the netmail is for is the whole point of writing it, so nothing is
    // guessed here.
    CHECK(fields.toName.empty());
    CHECK(fields.toAddr.empty());
    CHECK(fields.subject.empty());
}

TEST_CASE("A new netmail falls back to the main address [compose]") {
    // A tosser config with no AKA for the area. AreaManager normally fills the
    // config's address in on the way through; this covers the case where it
    // has not, so the rule holds wherever the area came from.
    const auto fields =
        amberedit::app::newMessage(editorConfig(), areaOf(AreaKind::Netmail, ""));
    CHECK(fields.fromAddr == "2:5020/9999.1");
}

TEST_CASE("A new echomail message is addressed to All [compose]") {
    const auto fields =
        amberedit::app::newMessage(editorConfig(), areaOf(AreaKind::Echo, "2:5020/736"));

    CHECK_FALSE(fields.netmail);
    CHECK(fields.fromName == "Yegor Gluhov");
    CHECK(fields.fromAddr == "2:5020/736");
    CHECK(fields.toName == "All");
    // Not applicable in an echo, and so not filled in whatever the message
    // being answered happens to carry.
    CHECK(fields.toAddr.empty());
}

TEST_CASE("A message starts local, and netmail private as well [compose]") {
    namespace attr = amberedit::domain::attr;
    const AppConfig cfg = editorConfig();

    // The two attributes nothing has to be asked about: the message is being written
    // here, and netmail is private as it has always been. Everything else is
    // left to the header screen.
    CHECK(amberedit::app::newMessage(cfg, areaOf(AreaKind::Echo, "2:5020/736"))
              .attributes == attr::kLocal);
    CHECK(amberedit::app::newMessage(cfg, areaOf(AreaKind::Netmail, "192:168/2"))
              .attributes == (attr::kLocal | attr::kPrivate));

    // A reply is a message like any other in this respect.
    const AreaConfig echo = areaOf(AreaKind::Echo, "2:5020/736");
    const AreaConfig netmail = areaOf(AreaKind::Netmail, "192:168/2");
    CHECK(amberedit::app::reply(cfg, echo, echo, messageFrom("Vasya Pupkin", "", ""))
              .attributes == attr::kLocal);
    CHECK(amberedit::app::reply(cfg, netmail, netmail,
                                messageFrom("Vasya Pupkin", "192:168/3", "192:168/2"))
              .attributes == (attr::kLocal | attr::kPrivate));
}

TEST_CASE("A message being changed keeps its own fields, and stops being sent "
          "[compose]") {
    namespace attr = amberedit::domain::attr;

    MessageHeader header =
        messageFrom("Vasya Pupkin", "192:168/3.1", /*destAddr=*/"192:168/2");
    header.to = "Yegor Gluhov";
    header.subject = "test";
    header.attributes = attr::kLocal | attr::kPrivate | attr::kSent | attr::kRead;

    const auto fields =
        amberedit::app::change(areaOf(AreaKind::Netmail, "2:5020/736"), header);

    CHECK(fields.changing);
    // The message's own header, not the user's: whoever wrote it is still who
    // it is from, and the area's AKA has nothing to say about it.
    CHECK(fields.fromName == "Vasya Pupkin");
    CHECK(fields.fromAddr == "192:168/3.1");
    CHECK(fields.toName == "Yegor Gluhov");
    CHECK(fields.toAddr == "192:168/2");
    CHECK(fields.subject == "test");
    // Every attribute it carries but MSGSENT: what went out is not what is being
    // written now.
    CHECK(fields.attributes == (attr::kLocal | attr::kPrivate | attr::kRead));
}

TEST_CASE("A netmail reply answers from the address it was sent to [compose]") {
    const auto header =
        messageFrom("Vasya Pupkin", "192:168/3.1", /*destAddr=*/"192:168/2");
    const AreaConfig netmail = areaOf(AreaKind::Netmail, "2:5020/736");
    const auto fields = amberedit::app::reply(editorConfig(), netmail, netmail, header);

    CHECK(fields.netmail);
    CHECK(fields.reply);
    CHECK(fields.fromName == "Yegor Gluhov");
    // 192:168/2 is an [akamatch] key, so it is ours to answer from — in
    // preference to the AKA the area is presented under.
    CHECK(fields.fromAddr == "192:168/2");
    CHECK(fields.toName == "Vasya Pupkin");
    CHECK(fields.toAddr == "192:168/3.1");
    CHECK(fields.subject == "test");
}

TEST_CASE("A netmail addressed elsewhere is answered from the [akamatch] AKA "
          "[compose]") {
    // Routed netmail, or a base shared with another point: the address it was
    // sent to is not ours, and answering from it would be forging it. There is
    // no AKA of ours to keep, so who the answer goes to picks one.
    const AreaConfig netmail = areaOf(AreaKind::Netmail, "2:5020/736");
    const auto fields = amberedit::app::reply(
        editorConfig(), netmail, netmail,
        messageFrom("Vasya Pupkin", "192:168/3.1", "2:5020/1"));

    CHECK(fields.fromAddr == "192:168/2");
    CHECK(fields.toAddr == "192:168/3.1");

    // And where no rule covers the recipient, the AKA the area is presented
    // under stays where it was put.
    const auto uncovered = amberedit::app::reply(
        editorConfig(), netmail, netmail,
        messageFrom("Vasya Pupkin", "2:382/736.120", "2:5020/1"));
    CHECK(uncovered.fromAddr == "2:5020/736");
}

TEST_CASE("A netmail reply with no destination address falls back [compose]") {
    const AreaConfig netmail = areaOf(AreaKind::Netmail, "2:5020/736");
    const auto header = messageFrom("Vasya Pupkin", "2:382/736.120", "");
    const auto fields = amberedit::app::reply(editorConfig(), netmail, netmail, header);
    CHECK(fields.fromAddr == "2:5020/736");
}

TEST_CASE("An echo answered into netmail picks the AKA off the recipient "
          "[compose]") {
    // The reader's `reply_to` into the netmail area. The message was posted to
    // an echo and written to nobody, whatever its destination field holds — a
    // tosser leaves this system's own address there often enough, the packet
    // having come addressed here — so the answer goes out from the AKA
    // [akamatch] names for whoever is being answered.
    const auto header =
        messageFrom("Vasya Pupkin", "192:200/1", /*destAddr=*/"2:5020/9999.1");
    const auto fields =
        amberedit::app::reply(editorConfig(), areaOf(AreaKind::Echo, "2:5020/736"),
                              areaOf(AreaKind::Netmail, "2:5020/736"), header);

    CHECK(fields.netmail);
    CHECK(fields.fromAddr == "192:168/2");
    CHECK(fields.toName == "Vasya Pupkin");
    CHECK(fields.toAddr == "192:200/1");
}

TEST_CASE("A netmail comment is addressed to the message's recipient [compose]") {
    // The whole of the To row comes from whoever the message was written to,
    // name and address together — the one thing a comment differs from a reply
    // in. The sender is the reply's: 192:168/2 is ours and is what the message
    // was written to, so that is what answers, whoever the answer goes to.
    auto header = messageFrom("Vasya Pupkin", "192:168/3.1", /*destAddr=*/"192:168/2");
    header.to = "Petya Ivanov";
    const AreaConfig netmail = areaOf(AreaKind::Netmail, "2:5020/736");
    const auto fields =
        amberedit::app::commentReply(editorConfig(), netmail, netmail, header);

    CHECK(fields.netmail);
    CHECK(fields.reply);
    CHECK(fields.fromName == "Yegor Gluhov");
    CHECK(fields.fromAddr == "192:168/2");
    CHECK(fields.toName == "Petya Ivanov");
    CHECK(fields.toAddr == "192:168/2");
    CHECK(fields.subject == "test");
}

TEST_CASE("A comment differs from the reply in the To row alone [compose]") {
    auto header = messageFrom("Vasya Pupkin", "192:168/3.1", "2:5020/1");
    header.to = "Petya Ivanov";
    const AreaConfig netmail = areaOf(AreaKind::Netmail, "2:5020/736");
    const auto config = editorConfig();
    const auto answer = amberedit::app::reply(config, netmail, netmail, header);
    const auto comment = amberedit::app::commentReply(config, netmail, netmail, header);

    // Routed netmail: the address it was sent to is not ours, so [akamatch]
    // picks the sender off whoever *wrote* it in both — the rule is about the
    // message being answered and not about who is being written to.
    CHECK(comment.fromAddr == answer.fromAddr);
    CHECK(comment.fromName == answer.fromName);
    CHECK(comment.subject == answer.subject);
    CHECK(comment.netmail == answer.netmail);
    CHECK(comment.reply == answer.reply);
    CHECK(comment.attributes == answer.attributes);

    CHECK(answer.toName == "Vasya Pupkin");
    CHECK(answer.toAddr == "192:168/3.1");
    CHECK(comment.toName == "Petya Ivanov");
    CHECK(comment.toAddr == "2:5020/1");
}

TEST_CASE("A netmail comment with no destination address leaves it empty [compose]") {
    // Nothing to be addressed to, as a reply to a message with no origin
    // address has nothing either: the field is left for the user to type.
    auto header = messageFrom("Vasya Pupkin", "2:382/736.120", "");
    header.to = "Petya Ivanov";
    const AreaConfig netmail = areaOf(AreaKind::Netmail, "2:5020/736");
    const auto fields =
        amberedit::app::commentReply(editorConfig(), netmail, netmail, header);

    CHECK(fields.toName == "Petya Ivanov");
    CHECK(fields.toAddr.empty());
    CHECK(fields.fromAddr == "2:5020/736");
}

TEST_CASE("An echomail comment is addressed to nobody in particular [compose]") {
    // An echo is written to a name and to no address, so that is all a comment
    // on it has to go by — "All" included, which is what most of an echo is
    // written to and what a comment on one of those is then addressed to.
    auto header = messageFrom("Vasya Pupkin", "2:382/736.120", "192:168/2");
    header.to = "All";
    const AreaConfig echo = areaOf(AreaKind::Echo, "2:5020/736");
    const auto fields = amberedit::app::commentReply(editorConfig(), echo, echo, header);

    CHECK_FALSE(fields.netmail);
    CHECK(fields.fromAddr == "2:5020/736");
    CHECK(fields.toName == "All");
    CHECK(fields.toAddr.empty());
    CHECK(fields.subject == "test");
}

TEST_CASE("An echomail reply answers from the area's AKA [compose]") {
    // The addresses in an echo's header belong to whoever wrote it and to the
    // link it arrived over; neither is anything to answer from or to.
    const auto header = messageFrom("Vasya Pupkin", "2:382/736.120", "192:168/2");
    const AreaConfig echo = areaOf(AreaKind::Echo, "2:5020/736");
    const auto fields = amberedit::app::reply(editorConfig(), echo, echo, header);

    CHECK_FALSE(fields.netmail);
    CHECK(fields.fromAddr == "2:5020/736");
    CHECK(fields.toName == "Vasya Pupkin");
    CHECK(fields.toAddr.empty());
    CHECK(fields.subject == "test");
}

TEST_CASE("The destination decides which AKA a netmail goes out under [compose]") {
    const auto config = editorConfig();

    const auto matched =
        amberedit::app::senderFor(config, *FtnAddress::parse("192:200/1"));
    REQUIRE(matched);
    CHECK(*matched == "192:168/2");

    // No rule covers it: the sender stays whatever the area put there, rather
    // than being pulled back to the main address.
    CHECK_FALSE(amberedit::app::senderFor(config, *FtnAddress::parse("2:382/736.120")));
}
