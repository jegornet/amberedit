#include <catch2/catch.hpp>

#include <string>

#include "encoding/charset_detector.hpp"

using amberedit::encoding::CharsetDetector;

namespace {
constexpr char kSoh = '\x01';
}

TEST_CASE("CharsetDetector normalises Fidonet charset names", "[charset]") {
    CHECK(CharsetDetector::normalize("CP866 2") == "CP866");
    CHECK(CharsetDetector::normalize("+7_FIDO 2") == "CP866");
    CHECK(CharsetDetector::normalize("LATIN-1 2") == "ISO-8859-1");
    CHECK(CharsetDetector::normalize("KOI8-R") == "KOI8-R");
    CHECK(CharsetDetector::normalize("UTF-8 4") == "UTF-8");
    CHECK(CharsetDetector::normalize("WINDOWS-1251") == "CP1251");
    CHECK(CharsetDetector::normalize("cp866") == "CP866");
}

TEST_CASE("CharsetDetector passes an unknown name through", "[charset]") {
    CHECK(CharsetDetector::normalize("CP1125 2") == "CP1125");
    CHECK(CharsetDetector::normalize("") == "");
}

TEST_CASE("CharsetDetector: IBMPC names no particular code page", "[charset]") {
    // FTS-5003 keeps IBMPC as an obsolete name for "some IBM PC code page":
    // CP866 here, CP437 or CP850 elsewhere. It cannot be mapped to one of them.
    CHECK(CharsetDetector::normalize("IBMPC 2") == "");
    CHECK(CharsetDetector::normalize("ibmpc") == "");
    CHECK_FALSE(CharsetDetector::namesSpecificCharset("IBMPC 2"));
    CHECK(CharsetDetector::namesSpecificCharset("CP866 2"));
}

TEST_CASE("CharsetDetector: IBMPC falls back to the default", "[charset]") {
    const std::string body = std::string(1, kSoh) + "CHRS: IBMPC 2\rтекст\r";

    CHECK(CharsetDetector("CP866").detect(body) == "CP866");
    // The same kludge in an area read as CP437 must not turn into CP866.
    CHECK(CharsetDetector("CP437").detect(body) == "CP437");
}

TEST_CASE("CharsetDetector: the default is the area's own", "[charset]") {
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

TEST_CASE("CharsetDetector: a default that names nothing usable", "[charset]") {
    // `default_charset = "IBMPC"` in the config is no better an answer than the
    // kludge is; iconv_open() would fail on an empty name, so CP866 stands in.
    CHECK(CharsetDetector("IBMPC").defaultCharset() == "CP866");
    CHECK(CharsetDetector("").defaultCharset() == "CP866");
    // Aliases are resolved once, in the constructor.
    CHECK(CharsetDetector("windows-1251").defaultCharset() == "CP1251");
}

TEST_CASE("CharsetDetector finds the CHRS kludge", "[charset]") {
    const std::string body = std::string(1, kSoh) + "MSGID: 2:5020/715 12345678\r" +
                             std::string(1, kSoh) + "CHRS: CP866 2\r" + "Привет!\r";

    CHECK(CharsetDetector::extractChrsKludge(body) == "CP866 2");
}

TEST_CASE("CharsetDetector understands the older CHARSET and CODEPAGE kludges",
          "[charset]") {
    CHECK(CharsetDetector::extractChrsKludge(std::string(1, kSoh) +
                                             "CHARSET: LATIN-1 2\r") == "LATIN-1 2");
    CHECK(CharsetDetector::extractChrsKludge(std::string(1, kSoh) + "CODEPAGE: 866\r") ==
          "866");
}

TEST_CASE("CharsetDetector does not mistake CHRS in the text for a kludge", "[charset]") {
    // Without a leading ^A this is an ordinary line of text, not a kludge.
    CHECK(CharsetDetector::extractChrsKludge("CHRS: KOI8-R 2\nтекст\n").empty());
}

TEST_CASE("CharsetDetector: CHRS outranks the default", "[charset]") {
    const CharsetDetector detector("CP866");
    const std::string body = std::string(1, kSoh) + "CHRS: KOI8-R 2\r текст";

    CHECK(detector.detect(body) == "KOI8-R");
}

TEST_CASE("CharsetDetector: no CHRS falls back to the default", "[charset]") {
    const CharsetDetector detector("KOI8-R");
    CHECK(detector.detect("plain text with no kludges") == "KOI8-R");

    const CharsetDetector defaultDetector;
    CHECK(defaultDetector.detect("text") == "CP866");
}
