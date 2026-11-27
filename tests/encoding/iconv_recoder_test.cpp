#include <doctest/doctest.h>

#include <string>

#include "encoding/iconv_recoder.hpp"
#include "test_strings.hpp"

using amberedit::encoding::IconvRecoder;
using amberedit::encoding::fitsCharset;
using amberedit::encoding::isValidUtf8;

namespace {

/// "Привет" in CP866.
const std::string kPrivetCp866 = "\x8F\xE0\xA8\xA2\xA5\xE2";
/// "Привет" in KOI8-R.
const std::string kPrivetKoi8 = "\xF0\xD2\xC9\xD7\xC5\xD4";
/// "Привет" in UTF-8.
const std::string kPrivetUtf8 = "Привет";

}  // namespace

TEST_CASE("IconvRecoder converts CP866 to UTF-8 [iconv]") {
    IconvRecoder recoder;
    CHECK(recoder.toUtf8(kPrivetCp866, "CP866") == kPrivetUtf8);
    CHECK(amberedit::test::errorOf(recoder.intoUtf8(kPrivetCp866, "CP866")).empty());
}

TEST_CASE("IconvRecoder converts KOI8-R to UTF-8 [iconv]") {
    IconvRecoder recoder;
    CHECK(recoder.toUtf8(kPrivetKoi8, "KOI8-R") == kPrivetUtf8);
    CHECK(amberedit::test::errorOf(recoder.intoUtf8(kPrivetKoi8, "KOI8-R")).empty());
}

TEST_CASE("IconvRecoder leaves valid UTF-8 alone [iconv]") {
    IconvRecoder recoder;
    CHECK(recoder.toUtf8(kPrivetUtf8, "UTF-8") == kPrivetUtf8);
}

TEST_CASE("IconvRecoder reuses the descriptor across calls [iconv]") {
    IconvRecoder recoder;
    for (int i = 0; i < 5; ++i) {
        CHECK(recoder.toUtf8(kPrivetCp866, "CP866") == kPrivetUtf8);
    }
    // Switching charsets on the fly must work too.
    CHECK(recoder.toUtf8(kPrivetKoi8, "KOI8-R") == kPrivetUtf8);
    CHECK(recoder.toUtf8(kPrivetCp866, "CP866") == kPrivetUtf8);
}

TEST_CASE("IconvRecoder handles an empty string [iconv]") {
    IconvRecoder recoder;
    CHECK(recoder.toUtf8("", "CP866").empty());
}

TEST_CASE("IconvRecoder survives an unknown charset [iconv]") {
    IconvRecoder recoder;
    // The text comes back unchanged — the message beats strictness about its
    // encoding — and the checked form says why, for the export and the import,
    // which must not accept an approximation.
    CHECK(recoder.toUtf8("abc", "NO-SUCH-CHARSET") == "abc");
    const std::string error =
        amberedit::test::errorOf(recoder.intoUtf8("abc", "NO-SUCH-CHARSET"));
    CHECK_MESSAGE(amberedit::test::contains(error, "NO-SUCH-CHARSET"), error);
}

TEST_CASE("IconvRecoder survives text larger than the buffer [iconv]") {
    IconvRecoder recoder;
    std::string input;
    std::string expected;
    for (int i = 0; i < 2000; ++i) {
        input += kPrivetCp866;
        expected += kPrivetUtf8;
    }
    CHECK(recoder.toUtf8(input, "CP866") == expected);
}

TEST_CASE("IconvRecoder is movable [iconv]") {
    IconvRecoder first;
    CHECK(first.toUtf8(kPrivetCp866, "CP866") == kPrivetUtf8);

    IconvRecoder second = std::move(first);
    CHECK(second.toUtf8(kPrivetCp866, "CP866") == kPrivetUtf8);
}

TEST_CASE("isValidUtf8 recognises well-formed UTF-8 [iconv]") {
    CHECK(isValidUtf8(""));
    CHECK(isValidUtf8("plain ascii"));
    CHECK(isValidUtf8(kPrivetUtf8));
    CHECK(isValidUtf8("日本語"));
    CHECK(isValidUtf8("\xF0\x9F\x99\x82"));  // U+1F642
}

TEST_CASE("isValidUtf8 rejects broken sequences [iconv]") {
    CHECK_FALSE(isValidUtf8(kPrivetCp866));    // single-byte Cyrillic
    CHECK_FALSE(isValidUtf8("\xC3"));          // truncated pair
    CHECK_FALSE(isValidUtf8("\xC0\x80"));      // overlong
    CHECK_FALSE(isValidUtf8("\xED\xA0\x80"));  // surrogate U+D800
    CHECK_FALSE(isValidUtf8("\xFF\xFE"));      // impossible bytes
}

TEST_CASE("fitsCharset says what a charset has room for [iconv]") {
    CHECK(fitsCharset(kPrivetUtf8, "CP866"));
    CHECK(fitsCharset(kPrivetUtf8, "KOI8-R"));
    CHECK_FALSE(fitsCharset(kPrivetUtf8, "CP437"));   // no Cyrillic there
    CHECK_FALSE(fitsCharset(kPrivetUtf8, "US-ASCII"));

    CHECK(fitsCharset("plain ascii", "CP437"));
    CHECK(fitsCharset("", "US-ASCII"));  // nothing fits everywhere
}

TEST_CASE("Everything fits UTF-8, and nothing fits a charset iconv has never heard of [iconv]") {
    CHECK(fitsCharset("日本語", "UTF-8"));
    CHECK(fitsCharset(kPrivetUtf8, "UTF8"));

    CHECK_FALSE(fitsCharset(kPrivetUtf8, "CP-NOT-A-CHARSET"));
    CHECK_FALSE(fitsCharset(kPrivetUtf8, ""));
}

TEST_CASE("fitsCharset does not answer yes by writing the text another way [iconv]") {
    // Which is what IconvRecoder::intoCharset() does — it spells Cyrillic out
    // in Latin, and so it succeeds at everything.
    IconvRecoder recoder;
    CHECK(recoder.fromUtf8(kPrivetUtf8, "CP437") == "Privet");
    CHECK_FALSE(fitsCharset(kPrivetUtf8, "CP437"));
}

TEST_CASE("Cyrillic a charset has no room for is spelt out in Latin [iconv][translit]") {
    IconvRecoder recoder;
    CHECK(recoder.fromUtf8("Ёжик в тумане", "CP437") == "Yozhik v tumane");
    CHECK(recoder.fromUtf8("ЩУКА и Щука", "ISO-8859-1") == "SCHUKA i Schuka");
    CHECK(recoder.fromUtf8("Привет, мир!", "US-ASCII") == "Privet, mir!");
}

TEST_CASE("A charset holding Cyrillic keeps its own letters [iconv][translit]") {
    IconvRecoder recoder;
    CHECK(recoder.fromUtf8(kPrivetUtf8, "CP866") == kPrivetCp866);
    CHECK(recoder.fromUtf8(kPrivetUtf8, "KOI8-R") == kPrivetKoi8);
    // One recoder written into both, as a base is: what one charset carries
    // is not remembered for the other.
    CHECK(recoder.fromUtf8(kPrivetUtf8, "CP437") == "Privet");
    CHECK(recoder.fromUtf8(kPrivetUtf8, "CP866") == kPrivetCp866);
}

TEST_CASE("Ukrainian letters are spelt out too, and what has no spelling is a "
          "question mark [iconv][translit]") {
    // KOI8-R has the Russian letters and neither Ї nor Ґ: those two alone are
    // spelt, and the rest of each word stays Cyrillic.
    IconvRecoder recoder;
    CHECK(recoder.fromUtf8("Їжак ґанок", "KOI8-R") ==
          recoder.fromUtf8("Yiжак gанок", "KOI8-R"));
    CHECK(recoder.fromUtf8("Їжак ґанок", "CP437") == "Yizhak ganok");
    CHECK(recoder.fromUtf8("日", "CP437") == "?");
}

TEST_CASE("Letters past U+04C0 are spelt out as well [iconv][translit]") {
    // ә and ө are led by D3 in UTF-8, beyond the run the Russian letters are in.
    IconvRecoder recoder;
    CHECK(recoder.fromUtf8("әлем өзен", "CP437") == "alem ozen");
}
