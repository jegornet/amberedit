#include <doctest/doctest.h>

#include <string>

#include "encoding/iconv_recoder.hpp"
#include "test_strings.hpp"

using amberedit::encoding::IconvRecoder;
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
