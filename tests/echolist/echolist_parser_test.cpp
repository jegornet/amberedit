#include "echolist/echolist_parser.hpp"

#include <doctest/doctest.h>

#include <string>

#include "encoding/iconv_recoder.hpp"
#include "test_strings.hpp"

using namespace amberedit;

TEST_CASE("the extension says which of the two shapes an echolist is in [echolist]") {
    using echolist::EcholistFormat;

    CHECK(echolist::formatOf("echo50.lst") == EcholistFormat::Lst);
    CHECK(echolist::formatOf("BACKBONE.NA") == EcholistFormat::Na);
    CHECK(echolist::formatOf("backbone.na") == EcholistFormat::Na);
    // The comma-separated list is the general shape; the two-column one has to
    // be asked for by name.
    CHECK(echolist::formatOf("ELIST.RPT") == EcholistFormat::Lst);

    // Only the two are unpacked from an archive at all.
    CHECK(echolist::isEcholistName("ELIST.NA"));
    CHECK(echolist::isEcholistName("echo50.lst"));
    CHECK_FALSE(echolist::isEcholistName("EDES2601.ZIP"));
    CHECK_FALSE(echolist::isEcholistName("README-Elist.txt"));
    CHECK_FALSE(echolist::isEcholistName("FILE_ID.DIZ"));
}

TEST_CASE("a .lst echolist gives up its tags and descriptions [echolist]") {
    const std::string text =
        "; [Status],Tag,Comment,Moderator's Name,Address,\r\n"
        ";\r\n"
        ",Ru.Linux,Linux and the rest of it,Some Body,2:5020/113,\r\n"
        ";S_bad_H\r\n"
        "Hold,RU.GAME.MMORPG,Online role playing,Someone Else,2:5030/1900.3,\r\n"
        ";CoModerator of Ru.Linux,Another Body, 2:50/101,\r\n";

    const auto parsed = echolist::parseEcholist(text, echolist::EcholistFormat::Lst);
    CHECK(parsed.warnings.empty());
    REQUIRE(parsed.entries.size() == 2);

    // The tag and the description are the second and third fields; the status,
    // the moderator and their address are for other programs than this one.
    CHECK(parsed.entries[0].tag == "Ru.Linux");
    CHECK(parsed.entries[0].description == "Linux and the rest of it");
    // A status in the first field changes nothing about the two that are read.
    CHECK(parsed.entries[1].tag == "RU.GAME.MMORPG");
    CHECK(parsed.entries[1].description == "Online role playing");
}

TEST_CASE("a .lst line that is not one is a warning against its number [echolist]") {
    const std::string text =
        ",Ru.Linux,Linux,Some Body,2:5020/113,\n"
        "this line has no commas at all\n"
        ",,a description with no echo to describe,Some Body,2:5020/113,\n";

    const auto parsed = echolist::parseEcholist(text, echolist::EcholistFormat::Lst);
    REQUIRE(parsed.entries.size() == 1);
    REQUIRE(parsed.warnings.size() == 2);
    CHECK(parsed.warnings[0].line == 2);
    CHECK(parsed.warnings[1].line == 3);
}

TEST_CASE("an echo an echolist says nothing about is not an entry [echolist]") {
    // Neither of the two shapes carries anything here, and neither is a mistake
    // anybody made: a list of tags with no descriptions is an ordinary file.
    const auto lst = echolist::parseEcholist(
        ",Ru.Linux,,Some Body,2:5020/113,\n"
        ",Ru.Unix,   ,Some Body,2:5020/113,\n",
        echolist::EcholistFormat::Lst);
    CHECK(lst.entries.empty());
    CHECK(lst.warnings.empty());

    const auto na =
        echolist::parseEcholist("FSX_ADS\nFSX_BBS   \n", echolist::EcholistFormat::Na);
    CHECK(na.entries.empty());
    CHECK(na.warnings.empty());
}

TEST_CASE("a .na echolist splits the tag off the description at the blanks "
          "[echolist]") {
    const std::string text =
        "AARP_FRAUD                            AARP Fraud Warning Network news\n"
        "; a comment, which both shapes are written with\n"
        "ALL-POLITICS                          Politics Unlimited\n"
        "\n"
        "AMATEUR_RADIO\tAmateur Radio / Ham Radio\n";

    const auto parsed = echolist::parseEcholist(text, echolist::EcholistFormat::Na);
    CHECK(parsed.warnings.empty());
    REQUIRE(parsed.entries.size() == 3);

    CHECK(parsed.entries[0].tag == "AARP_FRAUD");
    CHECK(parsed.entries[0].description == "AARP Fraud Warning Network news");
    CHECK(parsed.entries[1].tag == "ALL-POLITICS");
    // The description keeps its own spaces and loses only the run in front of
    // it, so a comma or a run of blanks inside one comes through as written.
    CHECK(parsed.entries[2].tag == "AMATEUR_RADIO");
    CHECK(parsed.entries[2].description == "Amateur Radio / Ham Radio");
}

TEST_CASE("a DOS end-of-file mark ends an echolist, and no echolist needs one "
          "[echolist]") {
    // Where one stands, the bytes after it are not the echolist.
    const auto ended = echolist::parseEcholist(
        ",Ru.Linux,Linux,Some Body,2:5020/113,\n"
        "\x1a"
        ",Ru.Unix,Unix,Some Body,2:5020/113,\n",
        echolist::EcholistFormat::Lst);
    REQUIRE(ended.entries.size() == 1);
    CHECK(ended.entries[0].tag == "Ru.Linux");

    // And where none does, the file is read to its end and nothing complains —
    // which is the ordinary case: none of the real echolists in testdata carries
    // one, in either of the two shapes.
    const auto plain = echolist::parseEcholist(
        ",Ru.Linux,Linux,Some Body,2:5020/113,\n"
        ",Ru.Unix,Unix,Some Body,2:5020/113,\n",
        echolist::EcholistFormat::Lst);
    CHECK(plain.entries.size() == 2);
    CHECK(plain.warnings.empty());

    const auto na = echolist::parseEcholist(
        "FSX_ADS   Ads + ANSI Art\nFSX_BBS   Support\n", echolist::EcholistFormat::Na);
    CHECK(na.entries.size() == 2);
    CHECK(na.warnings.empty());

    // A last line with no newline after it is a line like any other.
    const auto unterminated = echolist::parseEcholist(
        ",Ru.Linux,Linux,Some Body,2:5020/113,", echolist::EcholistFormat::Lst);
    CHECK(unterminated.entries.size() == 1);
}

TEST_CASE("a .lst description is cut at the comma the format has no escape for "
          "[echolist]") {
    // The format has no quoting and no escape, so a comma is a separator
    // wherever it stands: a description written with one in it is a line whose
    // author cut their own description short.
    const auto parsed = echolist::parseEcholist(
        ",Ru.Linux,Linux, kernels and all,Some Body,2:5020/113,\n",
        echolist::EcholistFormat::Lst);
    REQUIRE(parsed.entries.size() == 1);
    CHECK(parsed.entries[0].description == "Linux");
}

TEST_CASE("a warning quoting a line back does not cut a character in half "
          "[echolist]") {
    // The text has been decoded by the time the parser sees it, so a line
    // quoted back into a warning is UTF-8 — and a warning about a mangled line
    // must not itself reach the terminal mangled.
    // Cut at the 40th byte this lands in the middle of a two-byte character,
    // which is the whole of what `quoted()` is for.
    const std::string text =
        "Строка,которая не является строкой эхолиста и притом довольно длинная\n";

    const auto parsed = echolist::parseEcholist(text, echolist::EcholistFormat::Lst);
    REQUIRE(parsed.warnings.size() == 1);
    CHECK(encoding::isValidUtf8(parsed.warnings[0].message));
}
