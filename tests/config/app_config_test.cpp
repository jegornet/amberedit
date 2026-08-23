#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "config/app_config.hpp"
#include "config/text_util.hpp"
#include "domain/ftn_address.hpp"
#include "domain/message.hpp"
#include "test_paths.hpp"
#include "test_strings.hpp"

using amberedit::config::AppConfig;
using amberedit::config::TosserConfigFormat;
using amberedit::domain::FtnAddress;
using amberedit::test::contains;

namespace {

/// The required settings but the two that say whose config it is, kept apart
/// from them because a key written twice is a contradiction the parser refuses:
/// a test that writes its own `name` or `address` cannot be handed one here too.
const char* const kSettings =
    "tosser_config a\n"
    "tosser_config_format hpt\n"
    "default_charset CP866\n"
    "compose_charset CP866\n";
const char* const kName = "name Vasya Pupkin\n";
const char* const kAddress = "address 2:5020/9999.1\n";

/// The settings every config must state, which a test not about them still has
/// to write out for the config to load at all.
const std::string kRequired = kSettings + std::string(kName) + kAddress;

/// A config with the given body on top of the settings every config needs.
AppConfig with(const std::string& body) {
    return amberedit::test::valueOf(AppConfig::loadFromString(kRequired + body));
}

/// Why the same config would not load. Empty means it loaded.
std::string errorWith(const std::string& body) {
    return amberedit::test::errorOf(AppConfig::loadFromString(kRequired + body));
}

/// Whether it loaded at all, for the assertions that only say that much.
bool loads(const std::string& body) {
    return AppConfig::loadFromString(kRequired + body).has_value();
}

/// The same three for a body that writes the `name` itself, and then for one
/// that writes the `address` itself: the half it states is the half left out of
/// what is put in front of it.
AppConfig withOwnName(const std::string& body) {
    return amberedit::test::valueOf(
        AppConfig::loadFromString(kSettings + std::string(kAddress) + body));
}

std::string errorWithOwnName(const std::string& body) {
    return amberedit::test::errorOf(
        AppConfig::loadFromString(kSettings + std::string(kAddress) + body));
}

AppConfig withOwnAddress(const std::string& body) {
    return amberedit::test::valueOf(
        AppConfig::loadFromString(kSettings + std::string(kName) + body));
}

std::string errorWithOwnAddress(const std::string& body) {
    return amberedit::test::errorOf(
        AppConfig::loadFromString(kSettings + std::string(kName) + body));
}

}  // namespace

TEST_CASE("AppConfig parses a complete config [app_config]") {
    const auto cfg = amberedit::test::valueOf(AppConfig::loadFromString(R"(
    tosser_config /etc/husky/areas
    tosser_config_format fidoconfig
    default_charset KOI8-R
    compose_charset UTF-8

    name Иван Петров
    address 2:5020/9999.1
  )"));

    CHECK(cfg.tosserConfigPath == "/etc/husky/areas");
    CHECK(cfg.tosserConfigFormat == TosserConfigFormat::Fidoconfig);
    // The two are read independently: reading KOI8-R echoes is no reason to be
    // unable to write UTF-8.
    CHECK(cfg.defaultCharset == "KOI8-R");
    CHECK(cfg.composeCharset == "UTF-8");
    CHECK(cfg.userName == "Иван Петров");
    REQUIRE(cfg.userAddress.has_value());
    CHECK(cfg.userAddress->toString() == "2:5020/9999.1");
}

TEST_CASE("AppConfig: minimal config and defaults [app_config]") {
    // Everything the file has to say and nothing else, so that what is checked
    // under it is what AmberEdit fills in for a config that says no more.
    const auto cfg = amberedit::test::valueOf(
        AppConfig::loadFromString("tosser_config /etc/husky/areas.bbs\n"
                                  "tosser_config_format areas.bbs\n"
                                  "default_charset CP866\n"
                                  "compose_charset CP866\n"
                                  "name Vasya Pupkin\n"
                                  "address 2:382/9999.1\n"));

    CHECK(cfg.tosserConfigPath == "/etc/husky/areas.bbs");
    CHECK(cfg.tosserConfigFormat == TosserConfigFormat::AreasBbs);
    CHECK(cfg.userName == "Vasya Pupkin");
    REQUIRE(cfg.userAddress.has_value());
    CHECK(cfg.userAddress->toString() == "2:382/9999.1");
    // The AKA list is the `aka` lines and nothing more: the main address is not
    // one of them, however much it is one of ours.
    CHECK(cfg.akaMatches.empty());
    CHECK(cfg.origin.empty());
    CHECK(cfg.quoteString == " FL> ");
}

TEST_CASE("AppConfig requires both charsets [app_config]") {
    // Neither stands in for the other, and neither is guessed: what the echoes
    // one reads are written in says nothing about what one wants to write in,
    // and a default would mojibake silently in whichever direction it got
    // wrong. So each is named in the message that asks for it.
    const std::string base =
        "tosser_config a\ntosser_config_format hpt\n" + std::string(kName) + kAddress;
    const auto reason = [&base](const std::string& rest) {
        return amberedit::test::errorOf(AppConfig::loadFromString(base + rest));
    };

    const std::string error = reason("");
    CHECK_MESSAGE(contains(error, "default_charset is not set"), error);
    const std::string error2 = reason("default_charset CP866\n");
    CHECK_MESSAGE(contains(error2, "compose_charset is not set"), error2);
    const std::string error3 = reason("compose_charset CP866\n");
    CHECK_MESSAGE(contains(error3, "default_charset is not set"), error3);
    CHECK(reason("default_charset CP866\ncompose_charset UTF-8\n").empty());

    // An empty value is not a way to leave one out either.
    const std::string error4 = reason("default_charset \"\"\ncompose_charset CP866\n");
    CHECK_MESSAGE(contains(error4, "default_charset is not set"), error4);
}

TEST_CASE("AppConfig requires the name and the address [app_config]") {
    // Neither is guessed and neither has a default: an empty address is a
    // message with no From address and an origin line ending in an empty pair
    // of parentheses, which the tosser bounces, and an empty name leaves JAM
    // with no CRC to key a lastread record by, so that format silently keeps no
    // marks. Both are failures a long way from the config that caused them.
    const std::string base =
        "tosser_config a\ntosser_config_format hpt\n"
        "default_charset CP866\ncompose_charset CP866\n";
    const auto reason = [&base](const std::string& rest) {
        return amberedit::test::errorOf(AppConfig::loadFromString(base + rest));
    };

    const std::string error = reason("");
    CHECK_MESSAGE(contains(error, "name is not set"), error);
    const std::string error2 = reason(kName);
    CHECK_MESSAGE(contains(error2, "address is not set"), error2);
    const std::string error3 = reason(kAddress);
    CHECK_MESSAGE(contains(error3, "name is not set"), error3);
    CHECK(reason(std::string(kName) + kAddress).empty());

    // An empty value is not a way to leave the name out either. The address has
    // no such case: an empty value is no FTN address, and it is refused as one.
    const std::string error4 = reason("name \"\"\n" + std::string(kAddress));
    CHECK_MESSAGE(contains(error4, "name is not set"), error4);

    // A group states them for the areas it covers, and that is not a config
    // stating them: the group is reached only where the file already has both.
    const std::string error5 =
        reason("group\n member r50.sysop\n name Vasya Pupkin\nendgroup\n");
    CHECK_MESSAGE(contains(error5, "name is not set"), error5);
}

TEST_CASE("A value is quoted only when it has to be [app_config]") {
    // A name is several words and needs no quotes for it; quotes are what a
    // value whose own spaces matter is written in.
    CHECK(withOwnName("name Vasya Pupkin\n").userName == "Vasya Pupkin");
    CHECK(withOwnName("name \"Vasya Pupkin\"\n").userName == "Vasya Pupkin");
    CHECK(with("quote_string \" FL> \"\n").quoteString == " FL> ");

    // A "#" begins a comment where a word begins, and is a character like any
    // other inside a word or inside quotes.
    CHECK(withOwnName("name Vasya # our man\n").userName == "Vasya");
    CHECK(withOwnName("name \"Vasya #1\"\n").userName == "Vasya #1");
    CHECK(with("theme a#b.cfg\n").themePath == "a#b.cfg");

    const std::string error = errorWithOwnName("name \"Vasya\n");
    CHECK_MESSAGE(contains(error, "never closed"), error);
}

TEST_CASE("AppConfig accepts format aliases [app_config]") {
    const auto format = [](const std::string& name) {
        return amberedit::test::valueOf(
                   AppConfig::loadFromString("tosser_config a\ntosser_config_format " +
                                             name +
                                             "\ndefault_charset CP866\n"
                                             "compose_charset CP866\n" +
                                             kName + kAddress))
            .tosserConfigFormat;
    };
    CHECK(format("areas.bbs") == TosserConfigFormat::AreasBbs);
    CHECK(format("areasbbs") == TosserConfigFormat::AreasBbs);
    CHECK(format("hpt") == TosserConfigFormat::Fidoconfig);
    CHECK(format("squish") == TosserConfigFormat::SquishCfg);
    CHECK(format("squish.cfg") == TosserConfigFormat::SquishCfg);
    // Keys and the words they are given are read whatever case they are in.
    CHECK(format("Fidoconfig") == TosserConfigFormat::Fidoconfig);
}

TEST_CASE("AppConfig reads the link underline setting [app_config]") {
    CHECK(with("").underlineLinks);  // underlined unless the config says otherwise
    CHECK(with("reader_underline_links on\n").underlineLinks);
    CHECK_FALSE(with("reader_underline_links off\n").underlineLinks);
    CHECK_FALSE(loads("reader_underline_links 1\n"));
}

TEST_CASE("AppConfig reads the style codes setting [app_config]") {
    CHECK_FALSE(with("").styleCodes);  // off unless the config asks for it
    CHECK(with("reader_stylecodes on\n").styleCodes);
    CHECK_FALSE(with("reader_stylecodes off\n").styleCodes);
    CHECK_FALSE(loads("reader_stylecodes 1\n"));
}

TEST_CASE("AppConfig reads the BBS color codes setting [app_config]") {
    CHECK_FALSE(with("").bbsCodesRenegade);  // off unless the config asks for it
    CHECK(with("bbs_codes_renegade on\n").bbsCodesRenegade);
    CHECK_FALSE(with("bbs_codes_renegade off\n").bbsCodesRenegade);
    CHECK_FALSE(loads("bbs_codes_renegade 1\n"));
}

TEST_CASE("AppConfig reads the ANSI graphics setting [app_config]") {
    CHECK_FALSE(with("").bbsCodesAnsi);  // off unless the config asks for it
    CHECK(with("bbs_codes_ansi on\n").bbsCodesAnsi);
    CHECK_FALSE(with("bbs_codes_ansi off\n").bbsCodesAnsi);
    CHECK_FALSE(loads("bbs_codes_ansi 1\n"));
}

TEST_CASE("AppConfig reads the sender location setting [app_config]") {
    CHECK(with("").showLocation);  // shown unless the config says otherwise
    CHECK(with("show_location on\n").showLocation);
    CHECK_FALSE(with("show_location off\n").showLocation);
    CHECK_FALSE(loads("show_location 1\n"));
}

TEST_CASE("AppConfig reads the message size setting [app_config]") {
    CHECK_FALSE(with("").readerShowMessageSize);  // not shown unless asked for
    CHECK(with("reader_show_message_size on\n").readerShowMessageSize);
    CHECK_FALSE(with("reader_show_message_size off\n").readerShowMessageSize);
    CHECK_FALSE(loads("reader_show_message_size 1\n"));
}

TEST_CASE("AppConfig reads the unread highlight setting [app_config]") {
    CHECK(with("").highlightUnread);  // unread rows stand out unless told not to
    CHECK(with("highlight_unread on\n").highlightUnread);
    CHECK_FALSE(with("highlight_unread off\n").highlightUnread);
    CHECK_FALSE(loads("highlight_unread 1\n"));
}

TEST_CASE("AppConfig reads the edge exit setting [app_config]") {
    CHECK(with("").edgeExit);  // the ends leave the area unless the config says not
    CHECK(with("reader_edge_exit on\n").edgeExit);
    CHECK_FALSE(with("reader_edge_exit off\n").edgeExit);
    CHECK_FALSE(loads("reader_edge_exit 1\n"));
}

TEST_CASE("AppConfig reads the direct area reply setting [app_config]") {
    CHECK(with("").areaReplyDirect);  // followed unless the config says otherwise
    CHECK(with("areareplydirect on\n").areaReplyDirect);
    CHECK_FALSE(with("areareplydirect off\n").areaReplyDirect);
    CHECK_FALSE(loads("areareplydirect 1\n"));
    // Keys are read whatever case they are written in, and this one is usually
    // written the way GoldED writes it.
    CHECK_FALSE(with("AreaReplyDirect off\n").areaReplyDirect);
}

TEST_CASE("AppConfig reads the reply area setting [app_config]") {
    CHECK(with("").replyToArea.empty());  // no area named unless the config names one
    CHECK(with("reply_to_area NETMAIL\n").replyToArea == "NETMAIL");
    // A tag as it is written, checked against nothing: the area list does not
    // exist yet, and the dialog is what looks the name up.
    CHECK(with("reply_to_area no.such.area\n").replyToArea == "no.such.area");
}

TEST_CASE("AppConfig reads the two carbon copy list settings [app_config]") {
    using amberedit::config::CarbonList;
    using amberedit::config::CrosspostList;

    // The names, unless the config says otherwise: who else has the message is
    // worth saying, and their addresses are not what a reader of it wants.
    CHECK(with("").carbonList == CarbonList::Names);
    CHECK(with("compose_cc_list keep\n").carbonList == CarbonList::Keep);
    CHECK(with("compose_cc_list VISIBLE\n").carbonList == CarbonList::Visible);
    CHECK(with("compose_cc_list hidden\n").carbonList == CarbonList::Hidden);
    CHECK(with("compose_cc_list remove\n").carbonList == CarbonList::Remove);
    const std::string error = errorWith("compose_cc_list yes\n");
    CHECK_MESSAGE(contains(error, "compose_cc_list"), error);

    // The `XC:` list is spelled the way GoldED spells it, which is why the two
    // are not one enumeration with a value taken away.
    CHECK(with("").crosspostList == CrosspostList::Verbose);
    CHECK(with("compose_xc_list raw\n").crosspostList == CrosspostList::Raw);
    CHECK(with("compose_xc_list yes\n").crosspostList == CrosspostList::Yes);
    CHECK(with("compose_xc_list none\n").crosspostList == CrosspostList::None);
    const std::string error2 = errorWith("compose_xc_list hidden\n");
    CHECK_MESSAGE(contains(error2, "compose_xc_list"), error2);
}

TEST_CASE("AppConfig reads the lastread auto next setting [app_config]") {
    CHECK(with("").lastreadAutoNext);  // after the mark unless the config says otherwise
    CHECK(with("reader_lastread_auto_next on\n").lastreadAutoNext);
    CHECK_FALSE(with("reader_lastread_auto_next off\n").lastreadAutoNext);
    CHECK_FALSE(loads("reader_lastread_auto_next 1\n"));
}

TEST_CASE("AppConfig reads the scrollbar setting [app_config]") {
    CHECK(with("").showScrollbar);  // shown unless the config says otherwise
    CHECK(with("reader_scrollbar on\n").showScrollbar);
    CHECK_FALSE(with("reader_scrollbar off\n").showScrollbar);
    CHECK_FALSE(loads("reader_scrollbar yes\n"));
    CHECK_FALSE(loads("reader_scrollbar 1\n"));
}

TEST_CASE("AppConfig reads the list scrollbar settings [app_config]") {
    // Each answers for its own screen: the message list shows the bar unless the
    // config says otherwise, the area list only where it is asked for.
    CHECK_FALSE(with("").areaListScrollbar);
    CHECK(with("").messageListScrollbar);
    CHECK(with("arealist_scrollbar on\n").areaListScrollbar);
    CHECK_FALSE(with("arealist_scrollbar off\n").areaListScrollbar);
    CHECK(with("arealist_scrollbar off\n").messageListScrollbar);
    CHECK(with("msglist_scrollbar on\n").messageListScrollbar);
    CHECK_FALSE(with("msglist_scrollbar off\n").messageListScrollbar);
    CHECK(with("msglist_scrollbar off\narealist_scrollbar on\n").areaListScrollbar);
    CHECK_FALSE(loads("arealist_scrollbar yes\n"));
    CHECK_FALSE(loads("msglist_scrollbar 1\n"));
}

TEST_CASE("AppConfig reads the lastread user number [app_config]") {
    CHECK(with("").lastreadUser == 0);  // the single-user default
    CHECK(with("lastread_user 3\n").lastreadUser == 3);
    CHECK(with("lastread_user 65535\n").lastreadUser == 65535);
    // The number indexes an array grown by seeking past its end, so a wrong one
    // would quietly make a large sparse file rather than fail.
    CHECK_FALSE(loads("lastread_user -1\n"));
    CHECK_FALSE(loads("lastread_user 65536\n"));
    CHECK_FALSE(loads("lastread_user zero\n"));
}

TEST_CASE("AppConfig knows nothing of a header layout [app_config]") {
    // The header block has one layout now, so the setting that chose between
    // them is gone rather than accepted and ignored: a config still carrying it
    // says the reader is being asked for something it no longer does.
    CHECK_FALSE(loads("reader_header narrow\n"));
}

TEST_CASE("AppConfig reads the adaptive UI threshold [app_config]") {
    // Eighty is the width a terminal has always had, so it is what a config
    // saying nothing gets; stating it is what moves the line `when_narrow` and
    // `when_wide` are read against.
    CHECK(with("").adaptiveUiThreshold == 80);
    CHECK(with("adaptive_ui_threshold 100\n").adaptiveUiThreshold == 100);
    CHECK(with("adaptive_ui_threshold 20\n").adaptiveUiThreshold == 20);

    // A width no window is, either way round, is a mistyped one.
    CHECK_FALSE(loads("adaptive_ui_threshold 19\n"));
    CHECK_FALSE(loads("adaptive_ui_threshold 1001\n"));
    CHECK_FALSE(loads("adaptive_ui_threshold wide\n"));
    CHECK_FALSE(loads("adaptive_ui_threshold\n"));
    CHECK_FALSE(loads("adaptive_ui_threshold 80 100\n"));
}

TEST_CASE("AppConfig reads the hint bar setting [app_config]") {
    using amberedit::config::Visibility;

    // `on` unless the config says otherwise: there is no help screen, and the
    // narrow window with least room for the row is the one that needs it most.
    CHECK(with("").hintBar == Visibility::On);
    CHECK(with("hint_bar on\n").hintBar == Visibility::On);
    CHECK(with("hint_bar off\n").hintBar == Visibility::Off);
    CHECK(with("hint_bar when_narrow\n").hintBar == Visibility::WhenNarrow);
    CHECK(with("hint_bar when_wide\n").hintBar == Visibility::WhenWide);

    CHECK_FALSE(loads("hint_bar 1\n"));
    CHECK_FALSE(loads("hint_bar\n"));
    CHECK_FALSE(loads("hint_bar on off\n"));
}

TEST_CASE("AppConfig reads the back button setting [app_config]") {
    using amberedit::config::Visibility;

    // The window decides unless the config says otherwise, as it does for the
    // menu button; `on` and `off` are what settle it either way, and the two
    // window-led values are the one line read from its two sides.
    CHECK(with("").backButton == Visibility::WhenNarrow);
    CHECK(with("back_button on\n").backButton == Visibility::On);
    CHECK(with("back_button off\n").backButton == Visibility::Off);
    CHECK(with("back_button when_narrow\n").backButton == Visibility::WhenNarrow);
    CHECK(with("back_button when_wide\n").backButton == Visibility::WhenWide);

    CHECK_FALSE(loads("back_button 1\n"));
    CHECK_FALSE(loads("back_button none\n"));
    CHECK_FALSE(loads("back_button on off\n"));
    CHECK_FALSE(loads("back_button\n"));
    // `adaptive` was what `when_narrow` was called; a config still saying it is
    // one whose author has not been told which way round it now goes.
    CHECK_FALSE(loads("back_button adaptive\n"));
}

TEST_CASE("AppConfig reads whether the header block carries the Recd row "
          "[app_config]") {
    using amberedit::config::Visibility;

    // Off unless it is asked for — the one `Visibility` whose default is not
    // the window's: the row costs a fifth of the header block, and when a
    // message arrived here is rarely what it is being read for.
    CHECK(with("").showRecdDate == Visibility::Off);
    CHECK(with("show_recd_date on\n").showRecdDate == Visibility::On);
    CHECK(with("show_recd_date off\n").showRecdDate == Visibility::Off);
    CHECK(with("show_recd_date when_narrow\n").showRecdDate == Visibility::WhenNarrow);
    CHECK(with("show_recd_date when_wide\n").showRecdDate == Visibility::WhenWide);

    CHECK_FALSE(loads("show_recd_date 1\n"));
    CHECK_FALSE(loads("show_recd_date yes\n"));
    CHECK_FALSE(loads("show_recd_date\n"));
    CHECK_FALSE(loads("show_recd_date on off\n"));
}

TEST_CASE("AppConfig reads the menus [app_config]") {
    using amberedit::config::Command;

    const auto defaults = with("");
    CHECK(defaults.readerMenu ==
          std::vector<Command>{Command::ReaderList, Command::ReaderReply,
                                   Command::ReaderReplyElsewhere, Command::ReaderNew,
                                   Command::ReaderForward, Command::ReaderFind,
                                   Command::ReaderNodelist});
    CHECK(defaults.composeMenu ==
          std::vector<Command>{Command::ComposeSave, Command::ComposeImport});
    CHECK(with("compose_menu import save\n").composeMenu ==
          std::vector<Command>{Command::ComposeImport, Command::ComposeSave});

    // The buttons stand in the order they are written, which is the only order
    // there is to give them.
    CHECK(with("reader_menu new list\n").readerMenu ==
          std::vector<Command>{Command::ReaderNew, Command::ReaderList});

    // Changing a message that is already in the base, what the base holds about
    // it, and writing it out to a file are offered but not given: they are the
    // reader commands the default menu leaves out, and writing them down is what
    // puts them there.
    CHECK(with("reader_menu change info export\n").readerMenu ==
          std::vector<Command>{Command::ReaderChange, Command::ReaderInfo,
                                   Command::ReaderExport});

    // And so is answering the recipient rather than the sender: Alt-Q does it
    // without a button, and the button is there for whoever wants one.
    CHECK(with("reader_menu reply comment-reply\n").readerMenu ==
          std::vector<Command>{Command::ReaderReply, Command::ReaderCommentReply});

    // `none`, alone, is the menu asked for and left empty — which is a screen
    // with no menu button, there being nothing for one to open.
    CHECK(with("reader_menu none\n").readerMenu.empty());
    CHECK(with("compose_menu none\n").composeMenu.empty());
    CHECK_FALSE(loads("reader_menu none list\n"));

    // Each key knows its own commands: the editor's are not the reader's, and a
    // button that would do nothing is a mistake in the config rather than one
    // on the screen. A menu holds what a button can stand for, which is less
    // than every command the screen answers.
    CHECK_FALSE(loads("reader_menu save\n"));
    CHECK_FALSE(loads("compose_menu reply\n"));
    CHECK_FALSE(loads("reader_menu delete\n"));
    CHECK_FALSE(loads("compose_menu delete-line\n"));
    CHECK_FALSE(loads("reader_menu list list\n"));
    CHECK_FALSE(loads("reader_menu\n"));
    const std::string error = errorWith("reader_menu quit\n");
    REQUIRE_MESSAGE(
        contains(error,
                 "reply, reply-elsewhere, comment-reply, new, forward, change, "
                 "export, find, list, info, nodelist"),
        error);
}

TEST_CASE("AppConfig reads the hint bars [app_config]") {
    using amberedit::config::Command;

    const auto defaults = with("");
    CHECK(defaults.arealistHints ==
          std::vector<Command>{Command::AreaListNextUnread, Command::AreaListRescan});
    CHECK(defaults.msglistHints.empty());
    CHECK(defaults.readerHints ==
          std::vector<Command>{Command::ReaderReply, Command::ReaderReplyElsewhere,
                               Command::ReaderNew, Command::ReaderList,
                               Command::ReaderExport, Command::ReaderNodelist});
    CHECK(defaults.composeHints ==
          std::vector<Command>{Command::ComposeSave, Command::ComposeDeleteLine,
                               Command::ComposeImport});

    // The hints stand in the order they are written, and a row may name
    // anything the screen answers: a hint is a key with its name beside it, so
    // there is nothing a key does that a hint cannot say.
    CHECK(with("reader_hints info kludges\n").readerHints ==
          std::vector<Command>{Command::ReaderInfo, Command::ReaderKludges});
    CHECK(with("compose_hints word-left line-end\n").composeHints ==
          std::vector<Command>{Command::ComposeWordLeft, Command::ComposeLineEnd});

    // `app.quit` is answered before every screen, so every screen's row may
    // name it.
    CHECK(with("msglist_hints quit\n").msglistHints ==
          std::vector<Command>{Command::AppQuit});
    CHECK(with("arealist_hints quit rescan\n").arealistHints ==
          std::vector<Command>{Command::AppQuit, Command::AreaListRescan});

    // `none` is the row left empty, which is what the message list has until it
    // is given something.
    CHECK(with("arealist_hints none\n").arealistHints.empty());
    CHECK_FALSE(loads("arealist_hints none rescan\n"));

    // A screen's row names that screen's commands: the reader's are not the
    // editor's, a hint written twice is a slip, and a key with nothing after it
    // says nothing.
    CHECK_FALSE(loads("arealist_hints reply\n"));
    CHECK_FALSE(loads("reader_hints save\n"));
    CHECK_FALSE(loads("compose_hints reply\n"));
    CHECK_FALSE(loads("reader_hints list list\n"));
    CHECK_FALSE(loads("reader_hints\n"));
    const std::string error = errorWith("msglist_hints rescan\n");
    REQUIRE_MESSAGE(contains(error, "quit"), error);

    // The case the row is written in and where in the row it stands: one
    // setting each for every screen's row, the four of them being one row that
    // changes with the screen.
    CHECK_FALSE(with("").hintBarCapitalize);
    CHECK(with("hint_bar_capitalize on\n").hintBarCapitalize);
    CHECK_FALSE(with("hint_bar_capitalize off\n").hintBarCapitalize);
    CHECK_FALSE(loads("hint_bar_capitalize when_wide\n"));

    using amberedit::config::HintAlign;
    CHECK(with("").hintBarAlign == HintAlign::Center);
    CHECK(with("hint_bar_align left\n").hintBarAlign == HintAlign::Left);
    CHECK(with("hint_bar_align center\n").hintBarAlign == HintAlign::Center);
    CHECK(with("hint_bar_align right\n").hintBarAlign == HintAlign::Right);
    CHECK_FALSE(loads("hint_bar_align on\n"));
    CHECK_FALSE(loads("hint_bar_align\n"));
    CHECK_FALSE(loads("hint_bar_align left right\n"));
}

TEST_CASE("AppConfig reads whether the menu button is shown [app_config]") {
    using amberedit::config::Visibility;

    // It follows the window unless the config says otherwise, exactly as the
    // back button in the other corner does.
    CHECK(with("").menuButton == Visibility::WhenNarrow);
    CHECK(with("menu_button off\n").menuButton == Visibility::Off);
    CHECK(with("menu_button on\n").menuButton == Visibility::On);
    CHECK(with("menu_button when_narrow\n").menuButton == Visibility::WhenNarrow);
    CHECK(with("menu_button when_wide\n").menuButton == Visibility::WhenWide);

    CHECK_FALSE(loads("menu_button none\n"));
    CHECK_FALSE(loads("menu_button yes\n"));
    CHECK_FALSE(loads("menu_button adaptive\n"));
    CHECK_FALSE(loads("menu_button on off\n"));
    CHECK_FALSE(loads("menu_button\n"));
}

TEST_CASE("AppConfig reads how wide the menu's buttons stand [app_config]") {
    // Fifteen columns, frame and all, which is what the longest label the two
    // menus offer asks for.
    CHECK(with("").menuButtonsWidth == 22);
    CHECK(with("menu_buttons_width 20\n").menuButtonsWidth == 20);
    CHECK(with("menu_buttons_width 4\n").menuButtonsWidth == 4);

    // The floor is a frame with a column of label left inside it, under which a
    // button says nothing at all.
    CHECK_FALSE(loads("menu_buttons_width 3\n"));
    CHECK_FALSE(loads("menu_buttons_width 101\n"));
    CHECK_FALSE(loads("menu_buttons_width wide\n"));
    CHECK_FALSE(loads("menu_buttons_width\n"));
}

TEST_CASE("AppConfig reads how long a click is shown [app_config]") {
    CHECK(with("").clickAnimationMs == 100);
    CHECK(with("click_animation_ms 350\n").clickAnimationMs == 350);
    // Zero is a setting rather than a mistake: it is how the animation is
    // turned off, and a click then acts the moment it is read.
    CHECK(with("click_animation_ms 0\n").clickAnimationMs == 0);

    CHECK_FALSE(loads("click_animation_ms 1001\n"));
    CHECK_FALSE(loads("click_animation_ms -1\n"));
    CHECK_FALSE(loads("click_animation_ms briefly\n"));
}

TEST_CASE("AppConfig reads the wheel throttle [app_config]") {
    // On by default: every list that can be drawn more than one line to the row
    // is one the wheel would otherwise move two or three times too fast.
    CHECK(with("").listWheelThrottle);
    CHECK(with("").listWheelThrottleMs == 200);

    CHECK_FALSE(with("list_wheel_throttle off\n").listWheelThrottle);
    CHECK(with("list_wheel_throttle_ms 350\n").listWheelThrottleMs == 350);
    // Zero is a setting rather than a mistake: with no gap for two notches to
    // fall inside, no notch belongs to the run before it and every one of them
    // moves the cursor.
    CHECK(with("list_wheel_throttle_ms 0\n").listWheelThrottleMs == 0);

    CHECK_FALSE(loads("list_wheel_throttle sometimes\n"));
    CHECK_FALSE(loads("list_wheel_throttle\n"));
    CHECK_FALSE(loads("list_wheel_throttle_ms 2001\n"));
    CHECK_FALSE(loads("list_wheel_throttle_ms -1\n"));
    CHECK_FALSE(loads("list_wheel_throttle_ms briefly\n"));
}

TEST_CASE("AppConfig reads what a row of the area list holds [app_config]") {
    using amberedit::config::AreaFieldKind;
    using amberedit::config::AreaListFormat;
    using Line = amberedit::config::AreaListLine;

    const auto formatOf = [](const std::string& value) {
        return with("arealist_format " + value + "\n").areaListFormatNarrow;
    };
    const auto formatError = [](const std::string& value) {
        return errorWith("arealist_format " + value + "\n");
    };
    const auto wideFormatOf = [](const std::string& value) {
        return with("arealist_format " + value + "\n").areaListFormatWide;
    };

    // The default narrow row stands two lines tall: the name across whatever
    // the counts leave, and the description under it across whatever the star
    // leaves.
    CHECK(with("").areaListFormatNarrow ==
          AreaListFormat{Line{{AreaFieldKind::Echoid, 0},
                              {AreaFieldKind::Space, 1},
                              {AreaFieldKind::Total, 4},
                              {AreaFieldKind::Space, 1},
                              {AreaFieldKind::Unread, 4}},
                         Line{{AreaFieldKind::Description, 0},
                              {AreaFieldKind::Space, 1},
                              {AreaFieldKind::UnreadFlag, 1}}});
    // The default wide one is a single line: the description goes beside the
    // name, the two sharing what the counts leave.
    CHECK(with("").areaListFormatWide ==
          AreaListFormat{Line{{AreaFieldKind::Echoid, 0},
                              {AreaFieldKind::Space, 1},
                              {AreaFieldKind::Description, 0},
                              {AreaFieldKind::Space, 1},
                              {AreaFieldKind::Total, 4},
                              {AreaFieldKind::Space, 1},
                              {AreaFieldKind::Unread, 4},
                              {AreaFieldKind::UnreadFlag, 1}}});
    CHECK(formatOf("\"e c u\\nd n\"") == with("").areaListFormatNarrow);
    CHECK(wideFormatOf("\"e c u\\nd n\" \"e d c un\"") == with("").areaListFormatWide);

    // One format is every window's: a config written before the second value
    // existed says the same thing it always did.
    CHECK(wideFormatOf("\"e c un\"") == formatOf("\"e c un\""));

    // A format with no \n in it is the one line every row used to be.
    CHECK(formatOf("\"e c un\"") == AreaListFormat{Line{{AreaFieldKind::Echoid, 0},
                                                        {AreaFieldKind::Space, 1},
                                                        {AreaFieldKind::Total, 4},
                                                        {AreaFieldKind::Space, 1},
                                                        {AreaFieldKind::Unread, 4},
                                                        {AreaFieldKind::UnreadFlag, 1}}});

    // Two are the narrow window's and the wide one's, in that order.
    CHECK(formatOf("\"a e\" \"a e d\"") ==
          AreaListFormat{Line{{AreaFieldKind::Number, 4},
                              {AreaFieldKind::Space, 1},
                              {AreaFieldKind::Echoid, 0}}});
    CHECK(wideFormatOf("\"a e\" \"a e d\"") ==
          AreaListFormat{Line{{AreaFieldKind::Number, 4},
                              {AreaFieldKind::Space, 1},
                              {AreaFieldKind::Echoid, 0},
                              {AreaFieldKind::Space, 1},
                              {AreaFieldKind::Description, 0}}});

    // A width follows the letter it belongs to; a letter without one stands as
    // wide as its default.
    CHECK(formatOf("\"a e0 c3 u3n\"") ==
          AreaListFormat{Line{{AreaFieldKind::Number, 4},
                              {AreaFieldKind::Space, 1},
                              {AreaFieldKind::Echoid, 0},
                              {AreaFieldKind::Space, 1},
                              {AreaFieldKind::Total, 3},
                              {AreaFieldKind::Space, 1},
                              {AreaFieldKind::Unread, 3},
                              {AreaFieldKind::UnreadFlag, 1}}});

    // Every letter, with the default widths the table promises.
    CHECK(formatOf("aedgcun") == AreaListFormat{Line{{AreaFieldKind::Number, 4},
                                                     {AreaFieldKind::Echoid, 0},
                                                     {AreaFieldKind::Description, 0},
                                                     {AreaFieldKind::Group, 5},
                                                     {AreaFieldKind::Total, 4},
                                                     {AreaFieldKind::Unread, 4},
                                                     {AreaFieldKind::UnreadFlag, 1}}});
    // Case means nothing, as it means nothing in arealist_sort.
    CHECK(formatOf("AEDGCUN") == formatOf("aedgcun"));

    // A field written twice is a layout, not a slip: the same number may stand
    // at both ends of a row.
    CHECK(formatOf("\"a e a\"") == AreaListFormat{Line{{AreaFieldKind::Number, 4},
                                                       {AreaFieldKind::Space, 1},
                                                       {AreaFieldKind::Echoid, 0},
                                                       {AreaFieldKind::Space, 1},
                                                       {AreaFieldKind::Number, 4}}});

    // \n cuts the format into the lines a row is drawn on, and a line is a
    // layout of its own: what stands on one says nothing about the next.
    CHECK(formatOf("\"a\\ne\\nd\"") ==
          AreaListFormat{Line{{AreaFieldKind::Number, 4}},
                         Line{{AreaFieldKind::Echoid, 0}},
                         Line{{AreaFieldKind::Description, 0}}});
    // The backslash is the config's own text — it knows no escapes of its own —
    // so \N is read as \n is, the letters being read case-insensitively
    // throughout.
    CHECK(formatOf("\"a\\Ne\"") == formatOf("\"a\\ne\""));
    // A row that is to have a blank line in it asks for one: a line holding a
    // space is a line asked for, where an empty one is a \n written once too
    // often.
    CHECK(formatOf("\"e\\n \\nd\"") ==
          AreaListFormat{Line{{AreaFieldKind::Echoid, 0}},
                         Line{{AreaFieldKind::Space, 1}},
                         Line{{AreaFieldKind::Description, 0}}});

    const std::string error = errorWith("arealist_format\n");
    CHECK_MESSAGE(contains(error, "needs the fields"), error);
    // A gap is written inside the quotes: three values are three formats, and
    // there is no third window to give one to.
    const std::string error2 = formatError("e c un");
    CHECK_MESSAGE(contains(error2, "one format, or two"), error2);
    const std::string error3 = formatError("\"e x\"");
    CHECK_MESSAGE(contains(error3, "is not a field"), error3);
    // A width with no letter in front of it belongs to nothing.
    const std::string error4 = formatError("\"3e\"");
    CHECK_MESSAGE(contains(error4, "is not a field"), error4);
    const std::string error5 = formatError("e300");
    CHECK_MESSAGE(contains(error5, "255 columns"), error5);
    // A backslash that begins no \n is not a field either, at the end of the
    // format as anywhere else.
    const std::string error6 = formatError("\"e\\td\"");
    CHECK_MESSAGE(contains(error6, "is not a field"), error6);
    const std::string error7 = formatError("\"ed\\\"");
    CHECK_MESSAGE(contains(error7, "is not a field"), error7);
    // An empty line holds no fields, wherever in the row it stands — a trailing
    // \n costs a line and says nothing.
    const std::string error8 = formatError("\"e c\\n\"");
    CHECK_MESSAGE(contains(error8, "holds no fields"), error8);
    const std::string error9 = formatError("\"\\ne\"");
    CHECK_MESSAGE(contains(error9, "holds no fields"), error9);
    const std::string error10 = formatError("\"e\\n\\nd\"");
    CHECK_MESSAGE(contains(error10, "holds no fields"), error10);
    // A row taller than any format worth writing is a \n typed once too often.
    const std::string error11 = formatError("\"e\\ne\\ne\\ne\\ne\\ne\\ne\"");
    CHECK_MESSAGE(contains(error11, "more than 5 lines"), error11);
    // The wide format is read as closely as the narrow one.
    const std::string error12 = formatError("\"e c un\" \"e x\"");
    CHECK_MESSAGE(contains(error12, "is not a field"), error12);
}

TEST_CASE("AppConfig reads what a row of the message list holds [app_config]") {
    using amberedit::config::kAutoWidth;
    using amberedit::config::MsgFieldKind;
    using amberedit::config::MsgListFormat;
    using Line = amberedit::config::MsgListLine;

    const auto formatOf = [](const std::string& value) {
        return with("msglist_format " + value + "\n").messageListFormatNarrow;
    };
    const auto formatError = [](const std::string& value) {
        return errorWith("msglist_format " + value + "\n");
    };
    const auto wideFormatOf = [](const std::string& value) {
        return with("msglist_format " + value + "\n").messageListFormatWide;
    };

    // The default narrow row stands two lines tall: the number, the two names
    // and the stamp at a written fifteen on one, the subject on the next.
    CHECK(with("").messageListFormatNarrow ==
          MsgListFormat{Line{{MsgFieldKind::Number, kAutoWidth},
                             {MsgFieldKind::Space, 1},
                             {MsgFieldKind::From, 0},
                             {MsgFieldKind::Space, 1},
                             {MsgFieldKind::To, 0},
                             {MsgFieldKind::Space, 1},
                             {MsgFieldKind::Date, 15}},
                        Line{{MsgFieldKind::Subject, 0}}});
    // The default wide one is a single line, the subject beside the names.
    CHECK(with("").messageListFormatWide ==
          MsgListFormat{Line{{MsgFieldKind::Number, kAutoWidth},
                             {MsgFieldKind::Space, 1},
                             {MsgFieldKind::From, 20},
                             {MsgFieldKind::Space, 1},
                             {MsgFieldKind::To, 20},
                             {MsgFieldKind::Space, 1},
                             {MsgFieldKind::Subject, 0},
                             {MsgFieldKind::Space, 1},
                             {MsgFieldKind::Date, kAutoWidth}}});
    CHECK(formatOf("\"a f0 t0 d15\\ns\"") == with("").messageListFormatNarrow);
    CHECK(wideFormatOf("\"a f0 t0 d15\\ns\" \"a f t s d\"") ==
          with("").messageListFormatWide);

    // The letters and the widths they stand at where none is written: the names
    // at twenty, the subject taking what is left, and the number and the stamp
    // working their own out from the area and the stamps in it.
    CHECK(formatOf("aftsd") == MsgListFormat{Line{{MsgFieldKind::Number, kAutoWidth},
                                                  {MsgFieldKind::From, 20},
                                                  {MsgFieldKind::To, 20},
                                                  {MsgFieldKind::Subject, 0},
                                                  {MsgFieldKind::Date, kAutoWidth}}});
    // Case means nothing here either.
    CHECK(formatOf("AFTSD") == formatOf("aftsd"));

    // A width follows the letter it belongs to, and 0 is a width like any other
    // to write — including after a letter whose own default is worked out.
    CHECK(formatOf("\"a4 f8 s d0\"") == MsgListFormat{Line{{MsgFieldKind::Number, 4},
                                                           {MsgFieldKind::Space, 1},
                                                           {MsgFieldKind::From, 8},
                                                           {MsgFieldKind::Space, 1},
                                                           {MsgFieldKind::Subject, 0},
                                                           {MsgFieldKind::Space, 1},
                                                           {MsgFieldKind::Date, 0}}});

    // One format is every window's, as it is for the area list.
    CHECK(wideFormatOf("\"a f s\"") == formatOf("\"a f s\""));

    // \n cuts the format into the lines a row is drawn on.
    CHECK(formatOf("\"a f\\ns\\nd\"") ==
          MsgListFormat{Line{{MsgFieldKind::Number, kAutoWidth},
                             {MsgFieldKind::Space, 1},
                             {MsgFieldKind::From, 20}},
                        Line{{MsgFieldKind::Subject, 0}},
                        Line{{MsgFieldKind::Date, kAutoWidth}}});

    // The complaints are the area list's, in the message list's own words: the
    // two settings are read by the same code and say the same things about a
    // value that will not do.
    const std::string error = errorWith("msglist_format\n");
    CHECK_MESSAGE(contains(error, "msglist_format needs the fields"), error);
    const std::string error2 = formatError("a f s");
    CHECK_MESSAGE(contains(error2, "one format, or two"), error2);
    const std::string error3 = formatError("\"a e\"");
    CHECK_MESSAGE(contains(error3, "is not a field"), error3);
    const std::string error4 = formatError("f300");
    CHECK_MESSAGE(contains(error4, "255 columns"), error4);
    const std::string error5 = formatError("\"a f\\n\"");
    CHECK_MESSAGE(contains(error5, "holds no fields"), error5);
}

TEST_CASE("AppConfig reads the area list order [app_config]") {
    using amberedit::config::AreaSortCriterion;
    using amberedit::config::AreaSortKey;
    using Order = std::vector<AreaSortCriterion>;

    const auto sortOf = [](const std::string& value) {
        return with("arealist_sort " + value + "\n").areaListSort;
    };
    const auto sortError = [](const std::string& value) {
        return errorWith("arealist_sort " + value + "\n");
    };
    const Order ascending{{AreaSortKey::Address, false},
                          {AreaSortKey::Echoid, false},
                          {AreaSortKey::Group, false},
                          {AreaSortKey::Type, false},
                          {AreaSortKey::Unread, false}};

    // The default sorts the netmail areas to the top and alphabetically under it.
    CHECK(with("").areaListSort ==
          Order{{AreaSortKey::Type, false}, {AreaSortKey::Echoid, false}});

    // The modifier belongs to the letter after it, and only to that one.
    const Order mixed{{AreaSortKey::Unread, true}, {AreaSortKey::Echoid, false}};
    CHECK(sortOf("-u+e") == mixed);
    CHECK(sortOf("-ue") == mixed);

    // Case and the spaces between the letters mean nothing.
    CHECK(sortOf("-U +E") == mixed);
    CHECK(sortOf("- u + e") == mixed);

    CHECK(sortOf("aegtu") == ascending);
    CHECK(sortOf("AEGTU") == ascending);

    // An empty value is the one way to ask for the tosser config's own order,
    // so it has to be written out rather than left off the line.
    CHECK(sortOf("\"\"").empty());
    const std::string error = errorWith("arealist_sort\n");
    CHECK_MESSAGE(contains(error, "needs the letters"), error);

    const std::string error2 = sortError("x");
    CHECK_MESSAGE(contains(error2, "not a sort criterion"), error2);
    const std::string error3 = sortError("ee");
    CHECK_MESSAGE(contains(error3, "named twice"), error3);
    const std::string error4 = sortError("e-E");
    CHECK_MESSAGE(contains(error4, "named twice"), error4);
    const std::string error5 = sortError("+-e");
    CHECK_MESSAGE(contains(error5, "no criterion between"), error5);
    const std::string error6 = sortError("e-");
    CHECK_MESSAGE(contains(error6, "trailing +/-"), error6);
}

TEST_CASE("AppConfig reads the message template [app_config]") {
    CHECK(with("").templatePath.empty());  // nothing to compose with
    CHECK(with("template /etc/amberedit/msg.tpl\n").templatePath ==
          "/etc/amberedit/msg.tpl");
    const std::string error = errorWith("template\n");
    CHECK_MESSAGE(contains(error, "template"), error);
}

TEST_CASE("AppConfig reads the quote string [app_config]") {
    CHECK(with("").quoteString == " FL> ");  // GoldED's, and ours by default
    CHECK(with("quote_string \"XX> \"\n").quoteString == "XX> ");
    CHECK(with("quote_string >\n").quoteString == ">");

    // Exactly one '>': none and the line is not a quote to any reader, two and
    // a first-level quote goes out looking like a second-level one.
    const std::string error = errorWith("quote_string \" FL \"\n");
    CHECK_MESSAGE(contains(error, "exactly one '>'"), error);
    const std::string error2 = errorWith("quote_string \"\"\n");
    CHECK_MESSAGE(contains(error2, "exactly one '>'"), error2);
    const std::string error3 = errorWith("quote_string \" FL>> \"\n");
    CHECK_MESSAGE(contains(error3, "exactly one '>'"), error3);
    const std::string error4 = errorWith("quote_string \"> FL> \"\n");
    CHECK_MESSAGE(contains(error4, "exactly one '>'"), error4);
}

TEST_CASE("AppConfig reads the quote margin [app_config]") {
    CHECK(with("").quoteMargin == 78);  // inside the 79 FTN has always used
    CHECK(with("quote_margin 72\n").quoteMargin == 72);
    CHECK(with("quote_margin 20\n").quoteMargin == 20);
    CHECK(with("quote_margin 255\n").quoteMargin == 255);

    CHECK_FALSE(loads("quote_margin 19\n"));
    CHECK_FALSE(loads("quote_margin 0\n"));
    CHECK_FALSE(loads("quote_margin -1\n"));
    CHECK_FALSE(loads("quote_margin 256\n"));
    CHECK_FALSE(loads("quote_margin seventy\n"));
}

TEST_CASE("AppConfig reads the date and time formats [app_config]") {
    // The reader's header shows one string, so its date and time are one
    // setting; a template writes the two through tokens of their own, so they
    // are two.
    // The reader's default carries the zone the message states, the stamp
    // beside it saying nothing about which clock it is on; the template's two
    // are the date and the time as a quote header has always written them.
    CHECK(with("").readerDateTimeFormat == "%d %b %y %H:%M %z");
    CHECK(with("").templateDateFormat == "%d %b %y");
    CHECK(with("").templateTimeFormat == "%H:%M");

    CHECK(with("reader_datetime_format \"%Y-%m-%d %H:%M:%S\"\n").readerDateTimeFormat ==
          "%Y-%m-%d %H:%M:%S");
    CHECK(with("template_date_format %d.%m.%Y\n").templateDateFormat == "%d.%m.%Y");
    CHECK(with("template_time_format %H:%M:%S\n").templateTimeFormat == "%H:%M:%S");

    // %z is taken as it is written, and is checked like any other specifier: it
    // writes nothing here, the stamp the check is made on stating no zone, and
    // that is a stamp of one line like any other.
    CHECK(with("reader_datetime_format \"%d %b %y %H:%M %z\"\n").readerDateTimeFormat ==
          "%d %b %y %H:%M %z");

    // A format is free to say anything a strftime format may, punctuation and
    // words included — it is a line of text with % specifiers in it.
    CHECK(with("template_date_format \"on %A, %d %B %Y\"\n").templateDateFormat ==
          "on %A, %d %B %Y");

    // Nothing at all is a mistake rather than a way of asking for no stamp:
    // there is no way to ask for one, and a silently blank column would look
    // like a message with no date.
    const std::string error = errorWith("reader_datetime_format \"\"\n");
    CHECK_MESSAGE(contains(error, "needs a strftime format"), error);
    const std::string error2 = errorWith("template_date_format\n");
    CHECK_MESSAGE(contains(error2, "needs a value"), error2);
    // Blank is that same mistake spelled differently: the stamp is trimmed, so
    // a format of spaces writes nothing at all.
    const std::string error3 = errorWith("reader_datetime_format \" \"\n");
    CHECK_MESSAGE(contains(error3, "writes no stamp at all"), error3);
    // A format that writes an offset and nothing else is not blank, though it
    // is blank for a message stating no zone: it is judged on the message that
    // states one.
    CHECK(with("reader_datetime_format %z\n").readerDateTimeFormat == "%z");
    // A stamp stands in one cell of the header table, so it has to be one line:
    // %n and %t are the only two specifiers that are not.
    const std::string error4 = errorWith("reader_datetime_format \"%d%n%H\"\n");
    CHECK_MESSAGE(contains(error4, "more than one line"), error4);
}

TEST_CASE("AppConfig reads the tearline and the origin [app_config]") {
    // The default names the program through the template's own tokens, so that
    // the version is written down in one place and not here.
    CHECK(with("").tearline == "@longpid @version");
    CHECK(with("").origin.empty());

    CHECK(with("tearline \"my editor\"\n").tearline == "my editor");
    CHECK(with("origin A BBS somewhere\n").origin == "A BBS somewhere");
    CHECK(with("tearline \"\"\n").tearline.empty());  // "---" with nothing after it
}

TEST_CASE("AppConfig reads the import cut lines [app_config]") {
    // What FTN mail has always fenced an enclosed file off with, and the same
    // line at both ends unless the config says otherwise.
    CHECK(with("").importBegin == "=== Cut ===");
    CHECK(with("").importEnd == "=== Cut ===");

    CHECK(with("import_begin \"--- file follows ---\"\n").importBegin ==
          "--- file follows ---");
    CHECK(with("import_end \"--- ends ---\"\n").importEnd == "--- ends ---");
    // Empty is a value like any other: it is how a file goes in with no line
    // in front of it.
    CHECK(with("import_begin \"\"\n").importBegin.empty());
    CHECK(with("import_end \"\"\n").importEnd.empty());

    // A per-area setting, like the origin and the quote string: what a message
    // written in an area looks like is what a group is for.
    const auto grouped = with(
        "import_begin top\n"
        "group\n"
        "  member esp.*\n"
        "  import_begin \"--- corte ---\"\n"
        "  import_end \"--- fin ---\"\n"
        "endgroup\n");
    amberedit::domain::AreaConfig spanish;
    spanish.tag = "esp.charla";
    CHECK(grouped.effectiveFor(spanish).importBegin == "--- corte ---");
    CHECK(grouped.effectiveFor(spanish).importEnd == "--- fin ---");

    amberedit::domain::AreaConfig other;
    other.tag = "localnet";
    CHECK(grouped.effectiveFor(other).importBegin == "top");
    CHECK(grouped.effectiveFor(other).importEnd == "=== Cut ===");
}

TEST_CASE("AppConfig expands ~ in the template path [app_config]") {
    // The same treatment the theme gets: a path in a config is written the way
    // it is typed in a shell.
    const auto cfg = with("template ~/msg.tpl\n");
    const char* home = std::getenv("HOME");
    REQUIRE(home != nullptr);
    CHECK(cfg.templatePath == std::string(home) + "/msg.tpl");
}

TEST_CASE("AppConfig reads the nodelist lines in the order they were written "
          "[app_config]") {
    // The order is the precedence: the first nodelist to name an address is the
    // one that keeps it, so the list must come back as the file wrote it.
    const auto cfg = with(
        "nodelist ~/ftn/nodelist/nodelist.ndl\n"
        "nodelist ~/ftn/nodelist/Z2DAILY.999\n"
        "nodelist \"~/ftn/nodelist/with a space.999\"\n"
        "nodelist_db ~/ftn/nodelist/nodelist.db\n"
        "tmpdir ~/ftn/tmp\n");

    const char* home = std::getenv("HOME");
    REQUIRE(home != nullptr);
    const std::string at(home);

    REQUIRE(cfg.nodelistSources.size() == 3);
    CHECK(cfg.nodelistSources[0] == at + "/ftn/nodelist/nodelist.ndl");
    CHECK(cfg.nodelistSources[1] == at + "/ftn/nodelist/Z2DAILY.999");
    CHECK(cfg.nodelistSources[2] == at + "/ftn/nodelist/with a space.999");
    CHECK(cfg.nodelistDbPath == at + "/ftn/nodelist/nodelist.db");
    CHECK(cfg.tempDirPath == at + "/ftn/tmp");
}

TEST_CASE("AppConfig has no nodelist unless one is named [app_config]") {
    const auto cfg = with("");
    CHECK(cfg.nodelistSources.empty());
    CHECK(cfg.nodelistDbPath.empty());
    CHECK(cfg.tempDirPath.empty());

    // A compiled nodelist somebody else keeps up to date is a config that names
    // only the file, and that is not an error.
    CHECK(with("nodelist_db /ftn/nodelist.db\n").nodelistDbPath == "/ftn/nodelist.db");
}

TEST_CASE("AppConfig reads the error log path [app_config]") {
    const char* home = std::getenv("HOME");
    REQUIRE(home != nullptr);

    // A path like every other path in the config: written the way it is typed in
    // a shell, so a leading ~/ is the home directory.
    CHECK(with("error_log ~/ftn/amberr.log\n").errorLogPath ==
          std::string(home) + "/ftn/amberr.log");

    // Empty by default, and that is what says the log is off — there is no
    // second field for whether one is being kept.
    CHECK(with("").errorLogPath.empty());

    // An empty path would read back as the setting never having been written,
    // which a line that is there did not mean.
    const std::string error = errorWith("error_log \"\"\n");
    CHECK_MESSAGE(contains(error, "needs the path"), error);
    const std::string error2 = errorWith("error_log /a\nerror_log /b\n");
    CHECK_MESSAGE(contains(error2, "set twice"), error2);
}

TEST_CASE("AppConfig refuses a nodelist with nowhere to compile it to [app_config]") {
    const std::string error = errorWith("nodelist /ftn/nodelist/nodelist.ndl\n");
    CHECK_MESSAGE(contains(error, "nodelist_db is not set"), error);
    const std::string error2 = errorWith("nodelist\n");
    CHECK_MESSAGE(contains(error2, "needs a value"), error2);
    // An empty path is not a path, and it would otherwise read back as the
    // setting never having been written.
    const std::string error3 = errorWith("nodelist_db \"\"\n");
    CHECK_MESSAGE(contains(error3, "needs the path"), error3);
    // Two of them is the point of the key; two of anything else is still a
    // contradiction.
    const std::string error4 = errorWith("nodelist_db /a\nnodelist_db /b\n");
    CHECK_MESSAGE(contains(error4, "set twice"), error4);
    const std::string error5 = errorWith("tmpdir /a\ntmpdir /b\n");
    CHECK_MESSAGE(contains(error5, "set twice"), error5);
}

TEST_CASE("AppConfig reads the echolist lines in the order they were written "
          "[app_config]") {
    // The order is the precedence here too: the first echolist to name an echo
    // is the one that describes it.
    const auto cfg = with(
        "echolist ~/ftn/echolist/echo50.lst CP866\n"
        "echolist ~/ftn/echolist/elst2601.zip\n"
        "echolist \"~/ftn/echolist/with a space.lst\" KOI8-R\n"
        "echolist_db ~/ftn/amberecho.db\n");

    const char* home = std::getenv("HOME");
    REQUIRE(home != nullptr);
    const std::string at(home);

    REQUIRE(cfg.echolistSources.size() == 3);
    CHECK(cfg.echolistSources[0].path == at + "/ftn/echolist/echo50.lst");
    CHECK(cfg.echolistSources[0].charset == "CP866");
    // A line that states no charset is stored as the nothing it stated: what
    // the locale answers is worked out when the file is read.
    CHECK(cfg.echolistSources[1].path == at + "/ftn/echolist/elst2601.zip");
    CHECK(cfg.echolistSources[1].charset.empty());
    CHECK(cfg.echolistSources[2].path == at + "/ftn/echolist/with a space.lst");
    CHECK(cfg.echolistSources[2].charset == "KOI8-R");
    CHECK(cfg.echolistDbPath == at + "/ftn/amberecho.db");
}

TEST_CASE("AppConfig has no echolist unless one is named [app_config]") {
    const auto cfg = with("");
    CHECK(cfg.echolistSources.empty());
    CHECK(cfg.echolistDbPath.empty());
    CHECK(cfg.areaDescriptionPriority == amberedit::config::DescriptionPriority::Area);

    // A compiled echolist somebody else keeps up to date is a config that names
    // only the file, and that is not an error.
    CHECK(with("echolist_db /ftn/amberecho.db\n").echolistDbPath == "/ftn/amberecho.db");
}

TEST_CASE("AppConfig refuses an echolist with nowhere to compile it to [app_config]") {
    const std::string error = errorWith("echolist /ftn/echolist/echo50.lst\n");
    CHECK_MESSAGE(contains(error, "echolist_db is not set"), error);
    const std::string error2 = errorWith("echolist\n");
    CHECK_MESSAGE(contains(error2, "takes the path"), error2);
    // The path and the charset, and nothing after them.
    const std::string error3 = errorWith("echolist /a.lst CP866 KOI8-R\n");
    CHECK_MESSAGE(contains(error3, "takes the path"), error3);
    const std::string error4 = errorWith("echolist \"\"\n");
    CHECK_MESSAGE(contains(error4, "needs the path"), error4);
    const std::string error5 = errorWith("echolist_db /a\necholist_db /b\n");
    CHECK_MESSAGE(contains(error5, "set twice"), error5);
}

TEST_CASE("AppConfig reads which description an area with two is shown by "
          "[app_config]") {
    using amberedit::config::DescriptionPriority;

    CHECK(with("arealist_description_priority echolist\n").areaDescriptionPriority ==
          DescriptionPriority::Echolist);
    // Read without regard to case, as every word-valued setting is.
    CHECK(with("arealist_description_priority AREA\n").areaDescriptionPriority ==
          DescriptionPriority::Area);
    const std::string error = errorWith("arealist_description_priority tosser\n");
    CHECK_MESSAGE(contains(error, "expected area | echolist"), error);
}

TEST_CASE("AppConfig reads what stands in for a missing description [app_config]") {
    // Words unless the config says otherwise: the column says the description
    // is missing rather than reading as an unfinished row.
    CHECK(with("").areaDescriptionDefault == "no description");
    CHECK(with("arealist_description_default \"—\"\n").areaDescriptionDefault == "—");
    // Written empty it asks for the blank column, which is what the area list
    // showed before there was a setting for it.
    CHECK(with("arealist_description_default \"\"\n").areaDescriptionDefault.empty());
    // A line with no value at all is the one shape that is refused: the blank
    // column is worth saying out loud.
    const std::string error = errorWith("arealist_description_default\n");
    CHECK_MESSAGE(contains(error, "needs the text to show"), error);
}

TEST_CASE("AppConfig keeps the nodelist out of an area group [app_config]") {
    // The nodelist is the whole config's, as the tosser config is: an area is
    // not read against a nodelist of its own.
    const std::string error = errorWith("group\nmember *\nnodelist /a\nendgroup\n");
    CHECK_MESSAGE(contains(error, "not for one area"), error);
}

TEST_CASE("AppConfig refuses a setting it does not know [app_config]") {
    // A misspelled key would otherwise go back to its default in silence, which
    // is a hard thing to notice and a harder one to explain.
    const std::string error = errorWith("scrollbar false\n");
    CHECK_MESSAGE(contains(error, "unknown setting"), error);
    const std::string error2 = errorWith("group_width 12\n");
    CHECK_MESSAGE(contains(error2, "unknown setting"), error2);
}

TEST_CASE("AppConfig refuses a setting written twice [app_config]") {
    // Which of the two was meant is not ours to guess, and the one that lost
    // would be an invisible line in the file.
    const std::string error = errorWith("quote_margin 70\nquote_margin 72\n");
    CHECK_MESSAGE(contains(error, "set twice"), error);
}

TEST_CASE("AppConfig names the line a mistake is on [app_config]") {
    const std::string error =
        amberedit::test::errorOf(AppConfig::loadFromString("tosser_config a\n"
                                                           "tosser_config_format hpt\n"
                                                           "quote_margin nine\n",
                                                           "amberedit.cfg"));
    CHECK_MESSAGE(contains(error, "amberedit.cfg:3"), error);
}

TEST_CASE("AppConfig says what a toml config has left in it [app_config]") {
    // The format the file used to be written in, kept apart from "unknown
    // setting" so that a config from an older AmberEdit explains itself.
    const std::string error =
        amberedit::test::errorOf(AppConfig::loadFromString("[general]\n"));
    CHECK_MESSAGE(contains(error, "[section]"), error);
    const std::string error2 =
        amberedit::test::errorOf(AppConfig::loadFromString("tosser_config = \"a\"\n"));
    CHECK_MESSAGE(contains(error2, "old toml spelling"), error2);
}

TEST_CASE("AppConfig requires tosser_config [app_config]") {
    CHECK_FALSE(
        AppConfig::loadFromString("tosser_config_format fidoconfig\n").has_value());
    CHECK_FALSE(AppConfig::loadFromString("").has_value());
}

TEST_CASE("AppConfig requires an explicit tosser_config_format [app_config]") {
    // AmberEdit must not guess the format from the file contents.
    CHECK_FALSE(
        AppConfig::loadFromString("tosser_config /etc/husky/areas\n").has_value());
    CHECK_FALSE(
        AppConfig::loadFromString("tosser_config a\ntosser_config_format\n").has_value());
    CHECK_FALSE(AppConfig::loadFromString("tosser_config a\ntosser_config_format auto")
                    .has_value());
}

TEST_CASE("AppConfig complains about an unknown format [app_config]") {
    CHECK_FALSE(AppConfig::loadFromString("tosser_config a\ntosser_config_format golded")
                    .has_value());
}

TEST_CASE("AppConfig complains about a malformed FTN address [app_config]") {
    const std::string error = errorWithOwnAddress("address не_адрес\n");
    CHECK_MESSAGE(contains(error, "address is not an FTN address"), error);
}

namespace {

/// The example from the documentation, which is also the shape the feature was
/// asked for in.
const char* const kExample = R"(
akamatch 192:168/2 192:* 172:16/*
akamatch 2:382/736.120 2:382/736.*
aka 255:255/255.0
)";

/// Which AKA a netmail to `dest` is written from, as text.
std::string akaFor(const AppConfig& cfg, const std::string& dest) {
    const auto address = FtnAddress::parse(dest);
    REQUIRE(address);
    const auto aka = cfg.akaFor(*address);
    return aka ? aka->toString() : "-";
}

}  // namespace

TEST_CASE("AppConfig reads the aka and akamatch lines [app_config]") {
    const auto cfg = with(kExample);

    REQUIRE(cfg.akaMatches.size() == 3);
    std::string listed;
    for (const auto& entry : cfg.akaMatches) {
        listed += entry.aka.toString() + '(';
        for (const auto& pattern : entry.patterns) listed += pattern.toString() + ' ';
        listed += ") ";
    }
    CHECK(listed.find("192:168/2(192:* 172:16/* )") != std::string::npos);
    CHECK(listed.find("2:382/736.120(2:382/736.* )") != std::string::npos);
    // An AKA with no patterns is not an error: it is one we have, and one that
    // is never picked on its own.
    CHECK(listed.find("255:255/255() ") != std::string::npos);

    CHECK(cfg.userAddress->toString() == "2:5020/9999.1");
}

TEST_CASE("An AKA declared on its own can be given patterns later [app_config]") {
    // The two keys write into one list: an `aka` line says which addresses are
    // ours, an `akamatch` line what each of them is used for.
    const auto cfg = with(
        "aka 192:168/2\n"
        "akamatch 192:168/2 192:*\n"
        "akamatch 192:168/2 172:16/*\n");

    REQUIRE(cfg.akaMatches.size() == 1);
    CHECK(cfg.akaMatches.front().patterns.size() == 2);
    CHECK(akaFor(cfg, "172:16/1") == "192:168/2");
}

TEST_CASE("AppConfig picks the AKA whose pattern says the most [app_config]") {
    const auto cfg = with(kExample);

    CHECK(akaFor(cfg, "192:168/9") == "192:168/2");   // zone 192
    CHECK(akaFor(cfg, "172:16/1.5") == "192:168/2");  // zone 172 and net 16
    CHECK(akaFor(cfg, "2:382/736.7") == "2:382/736.120");
    CHECK(akaFor(cfg, "2:382/736") == "2:382/736.120");  // the boss himself

    // Nothing matches: the main address, which is what it is there for.
    CHECK(akaFor(cfg, "2:5020/1") == "2:5020/9999.1");
    CHECK(akaFor(cfg, "172:17/1") == "2:5020/9999.1");
    CHECK(akaFor(cfg, "255:255/255") == "2:5020/9999.1");
}

TEST_CASE("AppConfig: a more specific pattern beats a wider one [app_config]") {
    const auto cfg = with(
        "akamatch 2:5020/736.1 2:*\n"
        "akamatch 2:382/736.120 2:382/736.*\n");

    CHECK(akaFor(cfg, "2:382/736.120") == "2:382/736.120");
    CHECK(akaFor(cfg, "2:382/737.1") == "2:5020/736.1");
}

TEST_CASE("AppConfig refuses a malformed akamatch line [app_config]") {
    // A dropped entry would show up as netmail from the wrong address, so every
    // one of these stops AmberEdit rather than being skipped.
    const std::string error = errorWith("akamatch\n");
    CHECK_MESSAGE(contains(error, "needs an AKA"), error);
    const std::string error2 = errorWith("akamatch \"not an address\" 2:*\n");
    CHECK_MESSAGE(contains(error2, "not an address"), error2);
    const std::string error3 = errorWith("akamatch 2:5020/1\n");
    CHECK_MESSAGE(contains(error3, "address patterns"), error3);
    const std::string error4 = errorWith("akamatch 2:5020/1 2:382/\n");
    CHECK_MESSAGE(contains(error4, "2:382/"), error4);
    // An AKA is one address; the destinations belong on an akamatch line.
    const std::string error5 = errorWith("aka 2:5020/1 2:*\n");
    CHECK_MESSAGE(contains(error5, "aka takes one address"), error5);
}

TEST_CASE("AppConfig: akamatch has the main address to fall back to [app_config]") {
    // It asks for one and needs no check of its own to get it: `address` is
    // required of every config, so the fallback is there by the time an
    // akamatch line is read at all.
    CHECK(loads(kExample));
    // An AKA that is only named asks for nothing and so needs nothing.
    CHECK(with("aka 255:255/255.0\n").akaMatches.size() == 1);
}

TEST_CASE("The example config is one AmberEdit reads [app_config]") {
    // It ships as the thing to copy, so a setting renamed here and not there
    // would hand every new user a config that stops at startup. The paths in it
    // point at a system that is not this one, so only the parsing is checked.
    const auto path = amberedit::test::projectPath("amberedit.cfg.example");
    const auto cfg = amberedit::test::valueOf(AppConfig::loadFromString(
        amberedit::test::valueOf(amberedit::config::text::readFile(path)), path));

    CHECK(cfg.tosserConfigFormat == TosserConfigFormat::Fidoconfig);
    CHECK(cfg.userName == "Vasya Pupkin");
    CHECK(cfg.quoteString == " FL> ");
    CHECK(cfg.origin == "Somewhere in the world");
    CHECK(cfg.areaListSort == std::vector<amberedit::config::AreaSortCriterion>{
                                  {amberedit::config::AreaSortKey::Type, false},
                                  {amberedit::config::AreaSortKey::Echoid, false}});
    // The aka and akamatch lines ship commented out, and are meant to: they
    // name addresses that belong to nobody who copies the file, and an
    // uncommented one would have a new user sending netmail from an address
    // that is not theirs. What they do once uncommented is the test below.
    CHECK(cfg.akaMatches.empty());
}

TEST_CASE("The example config's AKA lines parse once uncommented [app_config]") {
    // Being commented out, nothing in the example itself reads them — a user
    // uncommenting them would be the first to find out whether they still
    // parse. So they are written out here, as the example has them, and read.
    const auto cfg = with(
        "aka 192:168/2\n"
        "aka 2:5020/999.1\n"
        "aka 255:255/255.0\n"
        "akamatch 192:168/2 192:* 172:16/*\n"
        "akamatch 2:5020/999.1 2:5020/999.*\n");

    // Three AKAs: two of them given destinations by an akamatch line, and one
    // named on its own.
    REQUIRE(cfg.akaMatches.size() == 3);
    CHECK(akaFor(cfg, "192:168/9") == "192:168/2");
    CHECK(akaFor(cfg, "172:16/1.5") == "192:168/2");
    CHECK(akaFor(cfg, "2:5020/999.7") == "2:5020/999.1");
    // 255:255/255.0 is named and given no patterns, so it is never picked on
    // its own: anything unmatched is written from the main address.
    CHECK(akaFor(cfg, "2:5020/1") == "2:5020/9999.1");
}

TEST_CASE("AppConfig reads the address_macro lines [app_config]") {
    // The example from the documentation, which is also the shape the feature
    // was asked for in.
    const auto cfg = with(
        "address_macro af,AreaFix,2:382/736,\"PASSWORD\",k/s\n"
        "address_macro ff, FileFix, 2:382/736.0, secret, pvt cra\n"
        "address_macro boss,Sysop,2:382/736\n");

    REQUIRE(cfg.addressMacros.size() == 3);

    const auto* areafix = cfg.addressMacroFor("af");
    REQUIRE(areafix != nullptr);
    CHECK(areafix->name == "AreaFix");
    CHECK(areafix->address.toString() == "2:382/736");
    REQUIRE(areafix->subject.has_value());
    CHECK(*areafix->subject == "PASSWORD");
    REQUIRE(areafix->attributes.has_value());
    CHECK(*areafix->attributes == amberedit::domain::attr::kKillSent);

    // The blanks around a field mean nothing, an unquoted subject is a subject
    // like any other, and several attributes are written with blanks between
    // them — a slash could not be told from the one inside "K/s".
    const auto* filefix = cfg.addressMacroFor("ff");
    REQUIRE(filefix != nullptr);
    CHECK(filefix->name == "FileFix");
    REQUIRE(filefix->subject.has_value());
    CHECK(*filefix->subject == "secret");
    REQUIRE(filefix->attributes.has_value());
    CHECK(*filefix->attributes ==
          (amberedit::domain::attr::kPrivate | amberedit::domain::attr::kCrash));

    // Neither is stated: the message keeps the subject and the attributes it
    // already carries.
    const auto* boss = cfg.addressMacroFor("boss");
    REQUIRE(boss != nullptr);
    CHECK_FALSE(boss->subject.has_value());
    CHECK_FALSE(boss->attributes.has_value());
}

TEST_CASE("An address macro is found whole and without regard to case [app_config]") {
    const auto cfg = with("address_macro AF,AreaFix,2:382/736\n");

    CHECK(cfg.addressMacroFor("af") != nullptr);
    CHECK(cfg.addressMacroFor("Af") != nullptr);
    // Trimmed, since the field it is typed into may be left with a blank on it.
    CHECK(cfg.addressMacroFor("  af  ") != nullptr);
    // Inside a name it is not a macro: an "af" found in "Olaf" would address
    // the message to the robot.
    CHECK(cfg.addressMacroFor("Olaf") == nullptr);
    CHECK(cfg.addressMacroFor("af areafix") == nullptr);
    CHECK(cfg.addressMacroFor("") == nullptr);
}

TEST_CASE("An address macro may name attributes without a subject [app_config]") {
    // The empty field between the commas: nothing was said about the subject,
    // and what the message already carries is left alone.
    const auto cfg = with("address_macro af,AreaFix,2:382/736,,k/s\n");

    const auto* macro = cfg.addressMacroFor("af");
    REQUIRE(macro != nullptr);
    CHECK_FALSE(macro->subject.has_value());
    REQUIRE(macro->attributes.has_value());
    CHECK(*macro->attributes == amberedit::domain::attr::kKillSent);
}

TEST_CASE("AppConfig refuses a malformed address_macro line [app_config]") {
    const std::string error = errorWith("address_macro af,AreaFix\n");
    CHECK_MESSAGE(contains(error, "a macro, a name and an address"), error);
    const std::string error2 =
        errorWith("address_macro af,AreaFix,2:382/736,subj,k/s,extra\n");
    CHECK_MESSAGE(contains(error2, "a macro, a name and an address"), error2);
    const std::string error3 = errorWith("address_macro ,AreaFix,2:382/736\n");
    CHECK_MESSAGE(contains(error3, "the word that is typed"), error3);
    const std::string error4 = errorWith("address_macro af,,2:382/736\n");
    CHECK_MESSAGE(contains(error4, "the name the message is addressed to"), error4);
    const std::string error5 = errorWith("address_macro af,AreaFix,not_an_address\n");
    CHECK_MESSAGE(contains(error5, "not an FTN address"), error5);
    const std::string error6 =
        errorWith("address_macro af,AreaFix,2:382/736,PWD,nonsense\n");
    CHECK_MESSAGE(contains(error6, "not a message attribute"), error6);
    // The one word that looks like an attribute and is none.
    const std::string error7 = errorWith("address_macro af,AreaFix,2:382/736,PWD,uns\n");
    CHECK_MESSAGE(contains(error7, "Loc set with Snt clear"), error7);
    // Two lines may name two macros, but not the same one twice: the line that
    // lost would be an invisible one.
    const std::string error8 = errorWith(
        "address_macro af,AreaFix,2:382/736\n"
        "address_macro AF,AreaFix,2:5020/1\n");
    CHECK_MESSAGE(contains(error8, "defined twice"), error8);
    // It addresses a node, and an area group covers areas: the setting belongs
    // outside one.
    const std::string error9 = errorWith(
        "group\nmember netmail\naddress_macro af,A,2:382/736\n"
        "endgroup\n");
    CHECK_MESSAGE(contains(error9, "not for one area"), error9);
}

TEST_CASE("The example config's address_macro lines parse once uncommented "
          "[app_config]") {
    // They ship commented out, as the AKA lines do and for the same reason —
    // they name addresses belonging to nobody who copies the file — so nothing
    // in the example itself reads them. Written out here as the example has
    // them, and read.
    const auto cfg = with(
        "address_macro af,AreaFix,2:382/736,\"PASSWORD\",k/s\n"
        "address_macro ff,FileFix,2:382/736,\"PASSWORD\"\n"
        "address_macro boss,Sysop,2:5020/999\n");

    REQUIRE(cfg.addressMacros.size() == 3);
    REQUIRE(cfg.addressMacroFor("af") != nullptr);
    CHECK(cfg.addressMacroFor("af")->address.toString() == "2:382/736");
    REQUIRE(cfg.addressMacroFor("ff") != nullptr);
    CHECK_FALSE(cfg.addressMacroFor("ff")->attributes.has_value());
    CHECK(cfg.addressMacroFor("boss") != nullptr);
}

TEST_CASE("AppConfig throws on a missing file [app_config]") {
    CHECK_FALSE(AppConfig::loadFromFile("/nonexistent/amberedit.cfg").has_value());
}

TEST_CASE("Startup insists on a template that can be read [app_config]") {
    // Both halves are checked as the program starts rather than when the editor
    // is first reached: a config that cannot compose should say so before
    // anyone sits down to write, and a path in a config says nothing about a
    // file on disk.
    const auto dir = std::filesystem::temp_directory_path();
    const auto configPath = dir / "amberedit_template_check.cfg";
    const auto templatePath = dir / "amberedit_template_check.tpl";
    std::filesystem::remove(templatePath);

    const auto write = [](const std::filesystem::path& path, const std::string& text) {
        std::ofstream out(path);
        out << text;
    };
    const std::string base =
        "tosser_config /etc/husky/areas.bbs\n"
        "tosser_config_format areas.bbs\n"
        "default_charset CP866\n"
        "compose_charset CP866\n" +
        std::string(kName) + kAddress;

    write(configPath, base);
    CHECK_FALSE(AppConfig::loadFromFile(configPath.string()).has_value());  // none named

    write(configPath, base + "template \"" + templatePath.string() + "\"\n");
    CHECK_FALSE(
        AppConfig::loadFromFile(configPath.string()).has_value());  // named, not there

    write(templatePath, "@newHello.\n");
    CHECK(AppConfig::loadFromFile(configPath.string()).has_value());

    std::filesystem::remove(configPath);
    std::filesystem::remove(templatePath);
}

// --- area groups -------------------------------------------------------------

namespace {

/// An area with nothing about it but its tag, which is all a group looks at.
amberedit::domain::AreaConfig area(const std::string& tag) {
    amberedit::domain::AreaConfig config;
    config.tag = tag;
    return config;
}

}  // namespace

TEST_CASE("A group gives its areas settings of their own [app_config]") {
    const auto cfg = with(
        "group\n"
        "  member esp.*\n"
        "  member pt.*\n"
        "  default_charset LATIN-1\n"
        "  compose_charset LATIN-1\n"
        "endgroup\n");

    REQUIRE(cfg.areaGroups.size() == 1);
    CHECK(cfg.areaGroups.front().members.size() == 2);

    // The file's own settings stand where no group covers the area.
    CHECK(cfg.defaultCharset == "CP866");
    CHECK(cfg.effectiveFor(area("ru.linux")).defaultCharset == "CP866");

    CHECK(cfg.effectiveFor(area("esp.argentina")).defaultCharset == "LATIN-1");
    CHECK(cfg.effectiveFor(area("pt.brasil")).composeCharset == "LATIN-1");
    // And what the group says nothing about it leaves alone.
    CHECK(cfg.effectiveFor(area("esp.argentina")).userName == cfg.userName);
}

TEST_CASE("Groups are laid over one another one setting at a time [app_config]") {
    const auto cfg = with(
        "origin Somewhere\n"
        "group\n"
        "  member *\n"
        "  origin Everywhere\n"
        "  compose_charset CP866\n"
        "endgroup\n"
        "group\n"
        "  member esp.*\n"
        "  compose_charset LATIN-1\n"
        "endgroup\n"
        "group\n"
        "  member esp.argentina\n"
        "  name Yegor Gluhov\n"
        "endgroup\n");

    const auto resolved = cfg.effectiveFor(area("esp.argentina"));
    // Each setting comes from the most particular group that states it, so all
    // three groups have their say and none of them repeats the others.
    CHECK(resolved.origin == "Everywhere");
    CHECK(resolved.composeCharset == "LATIN-1");
    CHECK(resolved.userName == "Yegor Gluhov");

    // A sister area takes the same two and not the third.
    CHECK(cfg.effectiveFor(area("esp.chile")).composeCharset == "LATIN-1");
    CHECK(cfg.effectiveFor(area("esp.chile")).userName == "Vasya Pupkin");
    // And one outside them both keeps the widest group's answer.
    CHECK(cfg.effectiveFor(area("ru.linux")).composeCharset == "CP866");
}

TEST_CASE("A group may say whether replies follow the AREA: line [app_config]") {
    // It is a per-area setting: whether a base's messages carry an AREA: line
    // worth following is a question about that base and not about the config.
    const auto cfg = with(
        "group\n"
        "  member dupes\n"
        "  areareplydirect off\n"
        "endgroup\n");

    CHECK(cfg.areaReplyDirect);
    CHECK(cfg.effectiveFor(area("ru.linux")).areaReplyDirect);
    CHECK_FALSE(cfg.effectiveFor(area("dupes")).areaReplyDirect);
}

TEST_CASE("A group may turn the BBS color codes on for its areas [app_config]") {
    // Which is the only way anybody sensibly turns them on: an echo written in
    // pipe codes is a particular echo, and `|` elsewhere is a character.
    const auto cfg = with(
        "group\n"
        "  member fsx.*\n"
        "  bbs_codes_renegade on\n"
        "endgroup\n");

    CHECK_FALSE(cfg.bbsCodesRenegade);
    CHECK_FALSE(cfg.effectiveFor(area("ru.linux")).bbsCodesRenegade);
    CHECK(cfg.effectiveFor(area("fsx.bbs")).bbsCodesRenegade);
}

TEST_CASE("A group may turn the ANSI graphics on for its areas [app_config]") {
    const auto cfg = with(
        "group\n"
        "  member fsx.*\n"
        "  bbs_codes_ansi on\n"
        "endgroup\n");

    CHECK_FALSE(cfg.bbsCodesAnsi);
    CHECK_FALSE(cfg.effectiveFor(area("ru.linux")).bbsCodesAnsi);
    CHECK(cfg.effectiveFor(area("fsx.bbs")).bbsCodesAnsi);
}

TEST_CASE("An address a group states is the area's, and one of ours [app_config]") {
    const auto cfg = withOwnAddress(
        "address 2:5020/9999.1\n"
        "group\n"
        "  member r50.sysop\n"
        "  address 2:5020/9999\n"
        "endgroup\n");

    CHECK(cfg.effectiveFor(area("r50.sysop")).userAddress->toString() == "2:5020/9999");
    CHECK(cfg.effectiveFor(area("ru.linux")).userAddress->toString() == "2:5020/9999.1");

    // It is one of ours wherever it turns up, which is what keeps a message
    // written under it from reading as somebody else's.
    CHECK(cfg.isOwnAddress(*FtnAddress::parse("2:5020/9999")));
    REQUIRE(cfg.groupsFor(area("r50.sysop")).size() == 1);
    CHECK(cfg.groupsFor(area("r50.sysop")).front()->states("address"));
    CHECK_FALSE(cfg.groupsFor(area("r50.sysop")).front()->states("origin"));
}

TEST_CASE("A group block is refused when it is malformed [app_config]") {
    const std::string error =
        errorWith("group esp\n member esp.*\n origin x\nendgroup\n");
    CHECK_MESSAGE(contains(error, "group takes no values"), error);
    const std::string error2 = errorWith("group\n group\n endgroup\nendgroup\n");
    CHECK_MESSAGE(contains(error2, "do not nest"), error2);
    const std::string error3 = errorWith("endgroup\n");
    CHECK_MESSAGE(contains(error3, "no group above it"), error3);
    const std::string error4 =
        errorWith("group\n member esp.*\n origin x\nendgroup all\n");
    CHECK_MESSAGE(contains(error4, "endgroup takes no values"), error4);
    const std::string error5 = errorWith("group\n member esp.*\n origin x\n");
    CHECK_MESSAGE(contains(error5, "never closed"), error5);
    const std::string error6 = errorWith("member esp.*\n");
    CHECK_MESSAGE(contains(error6, "only written inside a group"), error6);
    const std::string error7 = errorWith("group\n origin x\nendgroup\n");
    CHECK_MESSAGE(contains(error7, "no member line"), error7);
    const std::string error8 = errorWith("group\n member esp.*\nendgroup\n");
    CHECK_MESSAGE(contains(error8, "sets nothing"), error8);
    const std::string error9 = errorWith("group\n member\n origin x\nendgroup\n");
    CHECK_MESSAGE(contains(error9, "member needs an area tag"), error9);
}

TEST_CASE("A group states settings, and only the ones it may [app_config]") {
    // Layout is the whole config's, and saying so is worth its own message: a
    // theme in a group is a mistake about what a group is for, not a typo.
    const std::string error =
        errorWith("group\n member esp.*\n theme dark.cfg\nendgroup\n");
    CHECK_MESSAGE(contains(error, "for the whole config"), error);
    const std::string error2 =
        errorWith("group\n member esp.*\n arealist_sort ue\nendgroup\n");
    CHECK_MESSAGE(contains(error2, "for the whole config"), error2);
    const std::string error3 =
        errorWith("group\n member esp.*\n tosser_config /etc/areas\nendgroup\n");
    CHECK_MESSAGE(contains(error3, "for the whole config"), error3);
    // A key nobody knows gets the message it gets anywhere else.
    const std::string error4 = errorWith("group\n member esp.*\n orgin x\nendgroup\n");
    CHECK_MESSAGE(contains(error4, "unknown setting"), error4);
    // And a global-only key is named as one whether or not its value would
    // also have been refused: where it belongs is the first thing to say.
    const std::string error5 = errorWith("group\n member esp.*\n theme\nendgroup\n");
    CHECK_MESSAGE(contains(error5, "for the whole config"), error5);
    // Twice in one group is the contradiction it is anywhere else...
    const std::string error6 =
        errorWith("group\n member esp.*\n origin a\n origin b\nendgroup\n");
    CHECK_MESSAGE(contains(error6, "set twice in this group"), error6);
    // ...but the same key outside it is a different setting entirely, and two
    // groups saying it are two answers to two questions.
    CHECK(
        loads("origin a\n"
              "group\n  member esp.*\n  origin b\nendgroup\n"
              "group\n  member pt.*\n  origin c\nendgroup\n"));
}

TEST_CASE("A group may say where an echo's answers and copies belong [app_config]") {
    // The three settings a `CC:` line is read under: where its copies go, and
    // what the message keeps of the two commands. All three are worth stating
    // per echo, and none of them is a layout setting.
    const auto cfg = with(
        "group\n"
        "  member ru.*\n"
        "  reply_to_area netmail\n"
        "  compose_cc_list hidden\n"
        "  compose_xc_list none\n"
        "endgroup\n");

    const auto here = cfg.effectiveFor(area("ru.linux"));
    CHECK(here.replyToArea == "netmail");
    CHECK(here.carbonList == amberedit::config::CarbonList::Hidden);
    CHECK(here.crosspostList == amberedit::config::CrosspostList::None);
    // And everywhere else the file's own answer stands.
    CHECK(cfg.effectiveFor(area("de.talk")).replyToArea.empty());
}

TEST_CASE("A value a group cannot be read from stops AmberEdit at startup "
          "[app_config]") {
    // Read once while the config is, over a copy that is thrown away, so that a
    // mistake in a group is found at startup and not at the area it covers.
    const std::string error =
        errorWith("group\n member esp.*\n quote_margin 5\nendgroup\n");
    CHECK_MESSAGE(contains(error, "quote_margin"), error);
    const std::string error2 =
        errorWith("group\n member esp.*\n quote_string \">>\"\nendgroup\n");
    CHECK_MESSAGE(contains(error2, "exactly one '>'"), error2);
    const std::string error3 =
        errorWith("group\n member esp.*\n address 2:382/\nendgroup\n");
    CHECK_MESSAGE(contains(error3, "not an FTN address"), error3);
}

TEST_CASE("The example config's group blocks parse once uncommented [app_config]") {
    // They ship commented out — they name echoes nobody who copies the file
    // subscribes to — so a user uncommenting them would be the first to find
    // out whether they still parse. They are written out here as the example
    // has them, and read.
    const auto cfg = with(
        "group\n"
        "  member esp.*\n"
        "  member pt.*\n"
        "  default_charset LATIN-1\n"
        "  compose_charset LATIN-1\n"
        "endgroup\n"
        "group\n"
        "  member r50.sysop\n"
        "  name Yegor Gluhov\n"
        "endgroup\n");

    REQUIRE(cfg.areaGroups.size() == 2);
    CHECK(cfg.effectiveFor(area("pt.brasil")).defaultCharset == "LATIN-1");
    CHECK(cfg.effectiveFor(area("r50.sysop")).userName == "Yegor Gluhov");
    // A group covers what its members name and nothing else.
    CHECK(cfg.areaGroups.front().specificityFor("esp.argentina").has_value());
    CHECK_FALSE(cfg.areaGroups.front().specificityFor("ru.linux").has_value());
}

TEST_CASE("Two groups that cannot be told apart are refused [app_config]") {
    // The same pattern twice is the typo the check is mostly there for: both
    // groups cover the same areas and both set the same thing, and there is no
    // answer to which of them wins.
    const std::string error = errorWith(
        "group\n member esp.*\n origin a\nendgroup\n"
        "group\n member esp.*\n origin b\nendgroup\n");
    CHECK_MESSAGE(contains(error, "neither pattern is the more"), error);
    // Two spellings that say exactly as much and still cover esp.argentina
    // between them.
    const std::string error2 = errorWith(
        "group\n member esp.*\n origin a\nendgroup\n"
        "group\n member esp.?*\n origin b\nendgroup\n");
    CHECK_MESSAGE(contains(error2, "neither pattern is the more"), error2);

    // The same two settling different things are two answers to two questions.
    CHECK(
        loads("group\n  member esp.*\n  origin a\nendgroup\n"
              "group\n  member esp.*\n  tearline b\nendgroup\n"));
    // One that says more than the other is the whole point of the feature.
    CHECK(
        loads("group\n  member esp.*\n  origin a\nendgroup\n"
              "group\n  member esp.argentina\n  origin b\nendgroup\n"));
    // And two that could never cover one area cannot clash over it.
    CHECK(
        loads("group\n  member esp.*\n  origin a\nendgroup\n"
              "group\n  member pt.*\n  origin b\nendgroup\n"));
}

// --- twits -------------------------------------------------------------------

namespace {

/// A message with only the four fields a twit rule ever looks at.
amberedit::domain::MessageHeader letter(const std::string& from, const std::string& to,
                                        const std::string& subject,
                                        const std::string& address = {}) {
    amberedit::domain::MessageHeader header;
    header.from = from;
    header.to = to;
    header.subject = subject;
    if (!address.empty()) header.origAddr = *FtnAddress::parse(address);
    return header;
}

}  // namespace

TEST_CASE("AppConfig reads the twit lines [app_config]") {
    const auto cfg = with(
        "twit \"Ivan Ivanov\"\n"
        "twit Petr Petrov\n"
        "twit 2:5030/*\n"
        "twit_subj \"*for sale*\"\n"
        "twit_to off\n"
        "twit_mode skip\n");

    REQUIRE(cfg.twits.size() == 3);
    // Quoted or not is the same line: the values are joined by the single space
    // that separated them, exactly as `name` reads.
    CHECK(cfg.twits[0].name == "Ivan Ivanov");
    CHECK(cfg.twits[1].name == "Petr Petrov");
    CHECK_FALSE(cfg.twits[0].address.has_value());
    // The third one is an address, which is what a value holding a zone and a
    // colon is; the name is empty exactly then.
    REQUIRE(cfg.twits[2].address.has_value());
    CHECK(cfg.twits[2].name.empty());
    CHECK(cfg.twits[2].address->toString() == "2:5030/*");

    REQUIRE(cfg.twitSubjects.size() == 1);
    CHECK(cfg.twitSubjects[0] == "*for sale*");
    CHECK_FALSE(cfg.twitTo);
    CHECK(cfg.twitMode == amberedit::config::TwitMode::Skip);
}

TEST_CASE("A config says nothing about twits unless it says so [app_config]") {
    const auto cfg = with("");
    CHECK(cfg.twits.empty());
    CHECK(cfg.twitSubjects.empty());
    // The two settings that have an answer either way: both ends of a message
    // are looked at, and a twit is shown with its text behind the notice.
    CHECK(cfg.twitTo);
    CHECK(cfg.twitMode == amberedit::config::TwitMode::Blank);
    CHECK_FALSE(cfg.isTwit(letter("Ivan Ivanov", "All", "Anything")));
}

TEST_CASE("A twit name is matched whole and with wildcards [app_config]") {
    const auto cfg = with("twit \"Ivan Ivanov\"\ntwit \"*Spammer*\"\n");

    CHECK(cfg.isTwit(letter("Ivan Ivanov", "All", "x")));
    // Case is folded for ASCII, as it is everywhere else in this file.
    CHECK(cfg.isTwit(letter("IVAN IVANOV", "All", "x")));
    // Whole, so a name that merely holds one is not it: a rule to be found
    // inside a name says so with stars of its own, as the second line does.
    CHECK_FALSE(cfg.isTwit(letter("Ivan Ivanovich", "All", "x")));
    CHECK(cfg.isTwit(letter("A Spammer Of Note", "All", "x")));
}

TEST_CASE("A twit line covers both ends of a message, unless twit_to is off "
          "[app_config]") {
    const auto both = with("twit \"Ivan Ivanov\"\n");
    CHECK(both.isTwit(letter("Petr Petrov", "Ivan Ivanov", "x")));
    CHECK(both.isTwit(letter("Ivan Ivanov", "All", "x")));

    const auto senders = with("twit \"Ivan Ivanov\"\ntwit_to off\n");
    CHECK_FALSE(senders.isTwit(letter("Petr Petrov", "Ivan Ivanov", "x")));
    CHECK(senders.isTwit(letter("Ivan Ivanov", "All", "x")));

    // A subject has no direction to have an opinion about, so twit_to says
    // nothing about it.
    const auto subjects = with("twit_subj \"*sale*\"\ntwit_to off\n");
    CHECK(subjects.isTwit(letter("Petr Petrov", "All", "Everything on SALE")));
}

TEST_CASE("Any one twit line is enough to make a message one [app_config]") {
    // The lines are a list of things not worth reading, not a description of
    // one thing: a message matching any of them is a twit.
    const auto cfg = with("twit \"Ivan Ivanov\"\ntwit_subj \"*sale*\"\n");
    CHECK(cfg.isTwit(letter("Ivan Ivanov", "All", "Something worth reading")));
    CHECK(cfg.isTwit(letter("Petr Petrov", "All", "Everything on sale")));
    CHECK_FALSE(cfg.isTwit(letter("Petr Petrov", "All", "Something worth reading")));
}

TEST_CASE("A twit address is a pattern over the four numbers [app_config]") {
    const auto cfg = with("twit 2:5030/*\n");

    CHECK(cfg.isTwit(letter("Petr Petrov", "All", "x", "2:5030/1042")));
    CHECK(cfg.isTwit(letter("Petr Petrov", "All", "x", "2:5030/9999.7")));
    CHECK_FALSE(cfg.isTwit(letter("Petr Petrov", "All", "x", "2:5020/1042")));
    // A message carrying no address at all — every echomail message in a JAM
    // base — is not covered by a rule about addresses.
    CHECK_FALSE(cfg.isTwit(letter("Petr Petrov", "All", "x")));
}

TEST_CASE("A twit line that is no address is a name [app_config]") {
    // A '*' on its own is the name glob it was written as rather than "every
    // address there is": an address states a zone, and this states nothing.
    const auto everyone = with("twit *\n");
    REQUIRE(everyone.twits.size() == 1);
    CHECK(everyone.twits[0].name == "*");
    CHECK(everyone.isTwit(letter("Anybody At All", "All", "x")));

    // And a value holding a colon that is no address is a name too — there is
    // no third thing a twit line could be.
    const auto oddity = with("twit \"Ivan: the second\"\n");
    REQUIRE(oddity.twits.size() == 1);
    CHECK(oddity.twits[0].name == "Ivan: the second");
}

TEST_CASE("AppConfig refuses a malformed twit line [app_config]") {
    const std::string error = errorWith("twit\n");
    CHECK_MESSAGE(contains(error, "twit needs a name"), error);
    const std::string error2 = errorWith("twit_subj\n");
    CHECK_MESSAGE(contains(error2, "twit_subj needs a subject"), error2);
    const std::string error3 = errorWith("twit_mode quietly\n");
    CHECK_MESSAGE(contains(error3, "show | blank | skip | ignore | kill"), error3);
    const std::string error4 = errorWith("twit_to sometimes\n");
    CHECK_MESSAGE(contains(error4, "on"), error4);
    // The two that are a list may be written as often as one likes; the two
    // that are an answer may not.
    CHECK(loads("twit a\ntwit b\ntwit_subj x\ntwit_subj y\n"));
    const std::string error5 = errorWith("twit_mode skip\ntwit_mode blank\n");
    CHECK_MESSAGE(contains(error5, "set twice"), error5);
}

TEST_CASE("A group adds twits of its own [app_config]") {
    const auto cfg = with(
        "twit \"Ivan Ivanov\"\n"
        "group\n"
        "  member esp.*\n"
        "  twit \"Petr Petrov\"\n"
        "  twit \"Semen Semenov\"\n"
        "  twit_mode ignore\n"
        "endgroup\n");

    const auto covered = cfg.effectiveFor(area("esp.argentina"));
    // Added rather than put in place of them: the people one does not read
    // anywhere are not un-ignored by an echo having a name of its own to add.
    CHECK(covered.twits.size() == 3);
    CHECK(covered.isTwit(letter("Ivan Ivanov", "All", "x")));
    CHECK(covered.isTwit(letter("Petr Petrov", "All", "x")));
    CHECK(covered.twitMode == amberedit::config::TwitMode::Ignore);

    // And everywhere else the file's own line is the whole of it.
    const auto elsewhere = cfg.effectiveFor(area("ru.linux"));
    CHECK(elsewhere.twits.size() == 1);
    CHECK_FALSE(elsewhere.isTwit(letter("Petr Petrov", "All", "x")));
    CHECK(elsewhere.twitMode == amberedit::config::TwitMode::Blank);
}

TEST_CASE("A group may turn the twits off for its areas [app_config]") {
    // `show` is the one thing that says "not here": a group cannot take a twit
    // line off the list, and it does not need to.
    const auto cfg = with(
        "twit \"Ivan Ivanov\"\n"
        "twit_mode skip\n"
        "group\n"
        "  member r50.sysop\n"
        "  twit_mode show\n"
        "endgroup\n");

    CHECK(cfg.effectiveFor(area("r50.sysop")).twitMode ==
          amberedit::config::TwitMode::Show);
    CHECK(cfg.effectiveFor(area("ru.linux")).twitMode ==
          amberedit::config::TwitMode::Skip);
}

TEST_CASE("The example config's twit lines parse once uncommented [app_config]") {
    // They ship commented out — there is nobody AmberEdit would name for a user
    // who copies the file — so nothing in the example itself reads them. The
    // four spellings it shows are written out here as it has them, and read.
    const auto cfg = with(
        "twit \"Ivan Ivanov\"\n"
        "twit *Spammer*\n"
        "twit 2:5030/*\n"
        "twit_subj \"*for sale*\"\n");

    REQUIRE(cfg.twits.size() == 3);
    CHECK(cfg.isTwit(letter("Ivan Ivanov", "All", "x")));
    CHECK(cfg.isTwit(letter("Some Spammer Or Other", "All", "x")));
    CHECK(cfg.isTwit(letter("Petr Petrov", "All", "x", "2:5030/1042")));
    CHECK(cfg.isTwit(letter("Petr Petrov", "All", "Everything FOR SALE")));
    CHECK_FALSE(cfg.isTwit(letter("Petr Petrov", "All", "x", "2:5020/1042")));
}

// --- areas declared by hand --------------------------------------------------

TEST_CASE("An area ... endarea block declares an area of its own [app_config]") {
    const auto cfg = with(
        "area ru.linux\n"
        "  path        /ftn/msg/ru.linux\n"
        "  type        squish\n"
        "  kind        echo\n"
        "  description \"Linux по-русски\"\n"
        "  group_label A\n"
        "  address     2:5020/9999.1\n"
        "  link        2:5020/715 2:5020/716\n"
        "  link        2:5030/1042\n"
        "endarea\n");

    REQUIRE(cfg.manualAreas.size() == 1);
    const auto& declared = cfg.manualAreas.front();
    CHECK(declared.line == 7);  // the six required settings stand above it

    const auto& area = declared.area;
    CHECK(area.tag == "ru.linux");
    CHECK(area.path == "/ftn/msg/ru.linux");
    CHECK(area.type == amberedit::domain::MsgBaseType::Squish);
    CHECK(area.kind == amberedit::domain::AreaKind::Echo);
    CHECK(area.description == "Linux по-русски");
    CHECK(area.group == "A");
    CHECK(area.address.toString() == "2:5020/9999.1");
    REQUIRE(area.links.size() == 3);
    CHECK(area.links[0].toString() == "2:5020/715");
    CHECK(area.links[2].toString() == "2:5030/1042");
}

TEST_CASE("What an area block leaves out [app_config]") {
    const auto cfg = with(
        "area NOTES\n"
        "  path ~/ftn/msg/notes\n"
        "endarea\n"
        "area su.general\n"
        "  type passthrough\n"
        "endarea\n");

    REQUIRE(cfg.manualAreas.size() == 2);
    const auto& notes = cfg.manualAreas.front().area;
    // An echo unless the block says otherwise, and no type at all — which is
    // not a failure: FtnMsgBase works it out from the files on disk.
    CHECK(notes.kind == amberedit::domain::AreaKind::Echo);
    CHECK(notes.type == amberedit::domain::MsgBaseType::Unknown);
    CHECK(notes.description.empty());
    CHECK(notes.group.empty());
    CHECK_FALSE(notes.address.isValid());
    CHECK(notes.links.empty());
    // The same `~/` every other path setting expands.
    CHECK(notes.path.find('~') == std::string::npos);
    CHECK(notes.path.substr(notes.path.size() - 14) == "/ftn/msg/notes");

    const auto& passthrough = cfg.manualAreas.back().area;
    CHECK(passthrough.type == amberedit::domain::MsgBaseType::Passthrough);
    CHECK(passthrough.path.empty());
    CHECK(passthrough.isPassthrough());
}

TEST_CASE("An area block's kinds and types are the words the tosser configs use "
          "[app_config]") {
    const auto cfg = with(
        "area NETMAIL\n"
        "  path /ftn/msg/netmail\n"
        "  type msg\n"
        "  kind netmail\n"
        "endarea\n"
        "area BAD\n"
        "  path /ftn/msg/bad\n"
        "  type JAM\n"
        "  kind Bad\n"
        "endarea\n");

    CHECK(cfg.manualAreas[0].area.type == amberedit::domain::MsgBaseType::Sdm);
    CHECK(cfg.manualAreas[0].area.kind == amberedit::domain::AreaKind::Netmail);
    // Read without regard to case, as every other word in the config is.
    CHECK(cfg.manualAreas[1].area.type == amberedit::domain::MsgBaseType::Jam);
    CHECK(cfg.manualAreas[1].area.kind == amberedit::domain::AreaKind::Bad);
}

TEST_CASE("An area's own address is an AKA of ours [app_config]") {
    // The same answer a group's `address` gets: a message written under it is
    // still the user's own, and nothing picks it by destination.
    const auto cfg = withOwnAddress(
        "address 2:5020/9999\n"
        "area point.area\n"
        "  path /ftn/msg/point\n"
        "  address 2:5020/9999.1\n"
        "endarea\n");

    CHECK(cfg.isOwnAddress(*FtnAddress::parse("2:5020/9999.1")));
    CHECK_FALSE(cfg.akaMatching(*FtnAddress::parse("2:5020/715")).has_value());
}

TEST_CASE("An area block is refused for what it gets wrong [app_config]") {
    const std::string error = errorWith("area\nendarea\n");
    CHECK_MESSAGE(contains(error, "area takes the echotag"), error);
    const std::string error2 = errorWith("area a b\nendarea\n");
    CHECK_MESSAGE(contains(error2, "area takes the echotag"), error2);
    const std::string error3 = errorWith("area ru.linux\n path /a\n");
    CHECK_MESSAGE(contains(error3, "never closed"), error3);
    const std::string error4 = errorWith("endarea\n");
    CHECK_MESSAGE(contains(error4, "no area above it"), error4);
    const std::string error5 = errorWith("area ru.linux\n path /a\nendgroup\n");
    CHECK_MESSAGE(contains(error5, "write endarea"), error5);
    const std::string error6 = errorWith("group\n member *\n origin x\nendarea\n");
    CHECK_MESSAGE(contains(error6, "write endgroup"), error6);
    const std::string error7 = errorWith("area ru.linux\n group\nendarea\n");
    CHECK_MESSAGE(contains(error7, "do not nest"), error7);
    const std::string error8 = errorWith("group\n area x\nendgroup\n");
    CHECK_MESSAGE(contains(error8, "do not nest"), error8);

    const std::string error9 = errorWith("area ru.linux\n path /a\n path /b\nendarea\n");
    CHECK_MESSAGE(contains(error9, "path is set twice in this area"), error9);
    const std::string error10 =
        errorWith("area ru.linux\n path /a\n type squisj\nendarea\n");
    CHECK_MESSAGE(contains(error10, "is not a base type"), error10);
    const std::string error11 =
        errorWith("area ru.linux\n path /a\n kind echomail\nendarea\n");
    CHECK_MESSAGE(contains(error11, "is not an area kind"), error11);
    const std::string error12 =
        errorWith("area ru.linux\n path /a\n address x\nendarea\n");
    CHECK_MESSAGE(contains(error12, "not an FTN address"), error12);
    const std::string error13 = errorWith("area ru.linux\n path /a\n link\nendarea\n");
    CHECK_MESSAGE(contains(error13, "link needs the address"), error13);

    // A path is the one field an area cannot do without, and a passthrough is
    // the one area that cannot have one.
    const std::string error14 = errorWith("area ru.linux\nendarea\n");
    CHECK_MESSAGE(contains(error14, "states no path"), error14);
    const std::string error15 =
        errorWith("area ru.linux\n path /a\n type passthrough\nendarea\n");
    CHECK_MESSAGE(contains(error15, "is passthrough and names a path"), error15);

    // A setting is a group's to state, and anything else is a misspelling.
    const std::string error16 =
        errorWith("area ru.linux\n path /a\n default_charset KOI8-R\nendarea\n");
    CHECK_MESSAGE(contains(error16, "is a setting and not a field of an area"), error16);
    const std::string error17 = errorWith("area ru.linux\n path /a\n desc x\nendarea\n");
    CHECK_MESSAGE(contains(error17, "unknown area field 'desc'"), error17);
    const std::string error18 =
        errorWith("area ru.linux\n path /a\n group_label A\n group_label B\nendarea\n");
    CHECK_MESSAGE(contains(error18, "group_label is set twice"), error18);

    const std::string error19 = errorWith(
        "area ru.linux\n path /a\nendarea\n"
        "area RU.LINUX\n path /b\nendarea\n");
    CHECK_MESSAGE(contains(error19, "is declared twice"), error19);
}

TEST_CASE("The area list comes from the tosser config, from blocks, or from both "
          "[app_config]") {
    const std::string charsets =
        "default_charset CP866\ncompose_charset CP866\n" + std::string(kName) + kAddress;

    // No tosser at all is a config of nothing but blocks.
    const auto own = amberedit::test::valueOf(AppConfig::loadFromString(
        std::string(charsets) + "area NOTES\n  path /ftn/msg/notes\nendarea\n"));
    CHECK(own.tosserConfigPath.empty());
    CHECK(own.manualAreas.size() == 1);

    // Neither is a config that would open on an empty screen.
    const auto reason = [charsets](const std::string& rest) {
        return amberedit::test::errorOf(
            AppConfig::loadFromString(std::string(charsets) + rest));
    };

    const std::string error = reason("");
    CHECK_MESSAGE(contains(error, "there is no area list"), error);
    // A format is still required as soon as a tosser config is named...
    const std::string error2 = reason("tosser_config a\n");
    CHECK_MESSAGE(contains(error2, "tosser_config_format is not set"), error2);
    // ...and names nothing without one.
    const std::string error3 = reason(
        "tosser_config_format hpt\n"
        "area NOTES\n path /a\nendarea\n");
    CHECK_MESSAGE(contains(error3, "a config that is not set"), error3);
}

TEST_CASE("A group covers an area declared by hand like any other [app_config]") {
    const auto cfg = with(
        "area NOTES\n"
        "  path /ftn/msg/notes\n"
        "  type jam\n"
        "endarea\n"
        "group\n"
        "  member notes\n"
        "  compose_charset UTF-8\n"
        "endgroup\n");

    REQUIRE(cfg.manualAreas.size() == 1);
    CHECK(cfg.effectiveFor(cfg.manualAreas.front().area).composeCharset == "UTF-8");
}
