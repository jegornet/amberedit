#include <doctest/doctest.h>

#include <string>

#include "encoding/charset_detector.hpp"

using amberedit::encoding::CharsetDetector;

namespace {
constexpr char kSoh = '\x01';
}

TEST_CASE("CharsetDetector normalises Fidonet charset names [charset]") {
    CHECK(CharsetDetector::normalize("CP866 2") == "CP866");
    CHECK(CharsetDetector::normalize("+7_FIDO 2") == "CP866");
    CHECK(CharsetDetector::normalize("LATIN-1 2") == "ISO-8859-1");
    CHECK(CharsetDetector::normalize("KOI8-R") == "KOI8-R");
    CHECK(CharsetDetector::normalize("UTF-8 4") == "UTF-8");
    CHECK(CharsetDetector::normalize("WINDOWS-1251") == "CP1251");
    CHECK(CharsetDetector::normalize("cp866") == "CP866");
}

TEST_CASE("CharsetDetector passes an unknown name through [charset]") {
    CHECK(CharsetDetector::normalize("CP1125 2") == "CP1125");
    CHECK(CharsetDetector::normalize("") == "");
}

TEST_CASE("CharsetDetector: a malformed CHRS names nothing [charset]") {
    // "+7_FIDO 2" with the underscore lost in transit. What follows the name is
    // the level and the level is a number, so "FIDO" says the value is not a
    // CHRS value: the first word of a broken name is not a charset, and taking
    // it for one is how "+7" reaches iconv_open() instead of CP866.
    CHECK(CharsetDetector::normalize("+7 FIDO 2") == "");
    CHECK(CharsetDetector::normalize("KOI8 R 2") == "");
    CHECK_FALSE(CharsetDetector::namesSpecificCharset("+7 FIDO 2"));

    // A level is optional, and a name on its own is still a name.
    CHECK(CharsetDetector::normalize("CP866") == "CP866");
    CHECK(CharsetDetector::normalize("  CP866   2  ") == "CP866");
}

TEST_CASE("CharsetDetector: a malformed CHRS falls back to the default [charset]") {
    const std::string body = std::string(1, kSoh) + "TID: ParToss 1.10/W32\r" +
                             std::string(1, kSoh) + "CHRS: +7 FIDO 2\r" + "Привет!\r";

    CHECK(CharsetDetector("CP866").detect(body) == "CP866");
    CHECK(CharsetDetector("KOI8-R").detect(body) == "KOI8-R");
}

TEST_CASE("CharsetDetector: a charset iconv cannot open falls back too [charset]") {
    // The name is shaped like a CHRS value and means nothing to iconv, so there
    // is nothing to convert the message with. Handing the name on regardless is
    // how the reader ends up drawing undecoded CP866 as Latin-1.
    const std::string body = std::string(1, kSoh) + "CHRS: NONEXISTENT 2\r" + "Привет!\r";

    CHECK(CharsetDetector("CP866").detect(body) == "CP866");
    CHECK(CharsetDetector("KOI8-R").detect(body) == "KOI8-R");

    // A name this table has no entry for but iconv does is still the message's
    // own charset: the fallback answers for what cannot be converted, not for
    // what is merely unlisted here.
    CHECK(CharsetDetector("CP866").detect(std::string(1, kSoh) + "CHRS: CP1250 2\r") ==
          "CP1250");
}

TEST_CASE("CharsetDetector: IBMPC names no particular code page [charset]") {
    // FTS-5003 keeps IBMPC as an obsolete name for "some IBM PC code page":
    // CP866 here, CP437 or CP850 elsewhere. It cannot be mapped to one of them.
    CHECK(CharsetDetector::normalize("IBMPC 2") == "");
    CHECK(CharsetDetector::normalize("ibmpc") == "");
    CHECK_FALSE(CharsetDetector::namesSpecificCharset("IBMPC 2"));
    CHECK(CharsetDetector::namesSpecificCharset("CP866 2"));
}

TEST_CASE("CharsetDetector: IBMPC falls back to the default [charset]") {
    const std::string body = std::string(1, kSoh) + "CHRS: IBMPC 2\rтекст\r";

    CHECK(CharsetDetector("CP866").detect(body) == "CP866");
    // The same kludge in an area read as CP437 must not turn into CP866.
    CHECK(CharsetDetector("CP437").detect(body) == "CP437");
}

TEST_CASE("CharsetDetector: the default is the area's own [charset]") {
    // A detector belongs to one open base, and the base is built with the
    // charset that area is read in — the config's, or an area group's where one
    // covers the tag. So the per-area answer is simply the one it was built
    // with, and there is nothing to override afterwards.
    CharsetDetector detector("CP437");

    const std::string ibmpc = std::string(1, kSoh) + "CHRS: IBMPC 2\rtext\r";
    CHECK(detector.defaultCharset() == "CP437");
    CHECK(detector.detect(ibmpc) == "CP437");
    CHECK(detector.detect("text with no kludges") == "CP437");
    // A message that names its charset is still read as it says.
    CHECK(detector.detect(std::string(1, kSoh) + "CHRS: KOI8-R 2\rtext\r") == "KOI8-R");
}

TEST_CASE("CharsetDetector: the default has its aliases resolved [charset]") {
    // Once, in the constructor, so that what detect() answers with is a name
    // iconv takes however the config spelled it. A default that names no
    // charset in particular never arrives: the config refuses
    // `default_charset IBMPC` at the line, and nothing is guessed here.
    CHECK(CharsetDetector("windows-1251").defaultCharset() == "CP1251");
    CHECK(CharsetDetector("+7_FIDO").defaultCharset() == "CP866");
}

TEST_CASE("CharsetDetector finds the CHRS kludge [charset]") {
    const std::string body = std::string(1, kSoh) + "MSGID: 2:5020/715 12345678\r" +
                             std::string(1, kSoh) + "CHRS: CP866 2\r" + "Привет!\r";

    CHECK(CharsetDetector::extractChrsKludge(body) == "CP866 2");
}

TEST_CASE("CharsetDetector understands the older CHARSET and CODEPAGE kludges "
          "[charset]") {
    CHECK(CharsetDetector::extractChrsKludge(std::string(1, kSoh) +
                                             "CHARSET: LATIN-1 2\r") == "LATIN-1 2");
    CHECK(CharsetDetector::extractChrsKludge(std::string(1, kSoh) + "CODEPAGE: 866\r") ==
          "866");
}

TEST_CASE("CharsetDetector does not mistake CHRS in the text for a kludge [charset]") {
    // Without a leading ^A this is an ordinary line of text, not a kludge.
    CHECK(CharsetDetector::extractChrsKludge("CHRS: KOI8-R 2\nтекст\n").empty());
}

TEST_CASE("CharsetDetector: CHRS outranks the default [charset]") {
    const CharsetDetector detector("CP866");
    const std::string body = std::string(1, kSoh) + "CHRS: KOI8-R 2\r текст";

    CHECK(detector.detect(body) == "KOI8-R");
}

TEST_CASE("CharsetDetector: no CHRS falls back to the default [charset]") {
    const CharsetDetector detector("KOI8-R");
    CHECK(detector.detect("plain text with no kludges") == "KOI8-R");

    const CharsetDetector other("CP866");
    CHECK(other.detect("text") == "CP866");
}
