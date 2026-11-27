#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "encoding/translit.hpp"

using amberedit::encoding::transliterated;

namespace {

bool none(std::string_view) { return false; }

}  // namespace

TEST_CASE("Every Russian letter has a spelling [translit]") {
    CHECK(transliterated("абвгдеёжзийклмнопрстуфхцчшщъыьэюя", none) ==
          "abvgdeyozhzijklmnoprstufhcchshsch\"y'eyuya");
}

TEST_CASE("A capital takes the case of the word it stands in [translit]") {
    CHECK(transliterated("Жук", none) == "Zhuk");
    CHECK(transliterated("ЖУК", none) == "ZHUK");
    CHECK(transliterated("ВЯЗ", none) == "VYAZ");
    CHECK(transliterated("Я", none) == "Ya");
    CHECK(transliterated("ЭТО Я", none) == "ETO Ya");
    CHECK(transliterated("ЕЖ", none) == "EZH");
}

TEST_CASE("A letter the charset carries is left as it stands [translit]") {
    const auto onlyA = [](std::string_view letter) { return letter == "а"; };
    CHECK(transliterated("баба", onlyA) == "bаbа");  // the а are still Cyrillic
    CHECK(transliterated("ябба", onlyA) == "yabbа");
    CHECK(transliterated("мама", [](std::string_view) { return true; }) == "мама");
}

TEST_CASE("What is not a Cyrillic letter goes through untouched [translit]") {
    CHECK(transliterated("plain — «текст» 日本", none) == "plain — «tekst» 日本");
    CHECK(transliterated("", none).empty());
}

TEST_CASE("Serbian, Macedonian and Bulgarian letters are spelt out [translit]") {
    CHECK(transliterated("Ђорђе Љубљана Њиве Ћуприја Џеп", none) ==
          "Djordje Ljubljana Njive Cuprija Dzep");
    CHECK(transliterated("Ѓорѓи Ѕвезда Ќерка ѝ ѐ", none) == "Gjorgji Dzvezda Kjerka i e");
    CHECK(transliterated("ЉУБАВ", none) == "LJUBAV");
}

TEST_CASE("The Turkic Cyrillic letters are spelt out [translit]") {
    CHECK(transliterated("Қазақстан Өскемен Ұлытау Әлем", none) ==
          "Qazaqstan Oskemen Ulytau Alem");
    CHECK(transliterated("ғ җ ң ү ҳ ҷ һ ӣ ӯ", none) == "gh zh ng u h j h i u");
    // Bashkir, Chuvash, Sakha, then Altai, Khakas and Gagauz.
    CHECK(transliterated("ҙ ҫ ҡ ӑ ӗ ӳ ҕ ҥ", none) == "z s q a e u gh ng");
    CHECK(transliterated("ӧ ӱ ӓ ӌ", none) == "o u a ch");
    CHECK(transliterated("ҒАЗАЛ", none) == "GHAZAL");
    CHECK(transliterated("Ғазал", none) == "Ghazal");
}

TEST_CASE("A capital between U+04C1 and U+04CE is told by its odd code point "
          "[translit]") {
    CHECK(transliterated("Ӌай", none) == "Chaj");
    CHECK(transliterated("ӋАЙ", none) == "CHAJ");
}
