#include "encoding/base64.hpp"

#include <doctest/doctest.h>

#include <string>

using amberedit::encoding::fromBase64;
using amberedit::encoding::toBase64;

TEST_CASE("Base64 is RFC 4648's, padding and all [base64]") {
    CHECK(toBase64("") == "");
    CHECK(toBase64("f") == "Zg==");
    CHECK(toBase64("fo") == "Zm8=");
    CHECK(toBase64("foo") == "Zm9v");
    CHECK(toBase64("foobar") == "Zm9vYmFy");
}

TEST_CASE("Base64 comes back as the bytes it was made of [base64]") {
    const std::string text = "Привет, 2:382/736\n\x01MSGID: \xff";
    const auto back = fromBase64(toBase64(text));
    REQUIRE(back.has_value());
    CHECK(*back == text);
}

TEST_CASE("Base64 is read with its padding left off [base64]") {
    const auto back = fromBase64("Zm8");
    REQUIRE(back.has_value());
    CHECK(*back == "fo");
}

TEST_CASE("What is not base64 is refused rather than skipped [base64]") {
    CHECK_FALSE(fromBase64("Zm9v\nYmFy").has_value());
    CHECK_FALSE(fromBase64("Zm=v").has_value());
    CHECK_FALSE(fromBase64("Z").has_value());
}
