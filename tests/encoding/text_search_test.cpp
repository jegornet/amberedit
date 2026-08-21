#include <catch2/catch.hpp>

#include <string>
#include <vector>

#include "encoding/text_search.hpp"

using amberedit::encoding::TextMatch;
using amberedit::encoding::TextSearch;

namespace {

/// The stretches of `text` a search found, written out — the offsets are bytes,
/// so what they name is what would be painted.
std::vector<std::string> hits(const TextSearch& search, const std::string& text) {
    std::vector<std::string> found;
    for (const TextMatch& match : search.findAll(text)) {
        found.push_back(text.substr(match.begin, match.end - match.begin));
    }
    return found;
}

}  // namespace

TEST_CASE("A search finds what it was given, wherever it stands", "[text_search]") {
    const TextSearch search("dog", "CP437");
    CHECK(search.contains("the dog"));
    CHECK(search.contains("dogged"));
    CHECK(search.contains("dog"));
    CHECK_FALSE(search.contains("do g"));
    CHECK_FALSE(search.contains(""));
}

TEST_CASE("An empty query matches nothing at all", "[text_search]") {
    // Not everything: a search for no words has not been asked for, and a
    // scan answering "yes" for every message would stop on the one it started
    // from.
    const TextSearch search("", "CP866");
    CHECK(search.empty());
    CHECK_FALSE(search.contains("anything"));
    CHECK(search.findAll("anything").empty());
}

TEST_CASE("Case is folded, in ASCII and in Cyrillic alike", "[text_search]") {
    CHECK(TextSearch("HELLO", "CP437").contains("hello there"));
    CHECK(TextSearch("hello", "CP437").contains("HELLO THERE"));
    // Привет / ПРИВЕТ — the fold has to reach the Cyrillic block, since
    // <cctype> under the locale would either fold the whole high half of a
    // single-byte range or nothing above ASCII at all.
    CHECK(TextSearch("привет", "CP866").contains("ПРИВЕТ, мир"));
    CHECK(TextSearch("ПРИВЕТ", "KOI8-R").contains("привет, мир"));
    // Ё is outside the А-Я run and folds all the same.
    CHECK(TextSearch("ёлка", "CP866").contains("Ёлка"));
}

TEST_CASE("The offsets name the characters that matched", "[text_search]") {
    const TextSearch search("МИР", "CP866");
    CHECK(hits(search, "привет, мир!") == std::vector<std::string>{"мир"});

    // Two of them, and neither overlapping the other.
    const TextSearch aa("aa", "CP437");
    CHECK(hits(aa, "aaaa") == std::vector<std::string>{"aa", "aa"});
}

TEST_CASE("CP866 folds the letters a Russian keyboard confuses", "[text_search]") {
    // Н/H, р/p and у/y are the same glyph on a DOS screen and one layout apart
    // on the keyboard, so a word spelled half in each is ordinary.
    const TextSearch search("Нужно", "CP866");
    CHECK(search.contains("Hужно"));  // Latin H
    CHECK(search.contains("нужно"));  // and the case fold with it
    CHECK(search.contains("нyжно"));  // Latin y
    // Upper case and the quirks together: a Latin H and Cyrillic capitals for
    // the rest. Only the three pairs fold — the Cyrillic О is not the Latin O,
    // and nothing here pretends it is.
    CHECK(search.contains("HУЖНО"));
    CHECK_FALSE(search.contains("HYЖHO"));

    CHECK(TextSearch("привет", "CP866").contains("пpивет"));  // Latin p
}

TEST_CASE("Only CP866 carries those quirks", "[text_search]") {
    // In a western area they are six different letters, and folding them would
    // make "Hello" find "Нello".
    CHECK_FALSE(TextSearch("Нужно", "KOI8-R").contains("Hужно"));
    CHECK_FALSE(TextSearch("hello", "CP437").contains("нello"));
    // The name is compared the way every other charset name here is.
    CHECK(TextSearch("Нужно", "cp866").contains("Hужно"));
}

TEST_CASE("The charset can be changed under a standing query", "[text_search]") {
    // What a scan over an area does: the words are typed once and the charset
    // is the message's, which most areas never change.
    TextSearch search("Нужно", "KOI8-R");
    CHECK_FALSE(search.contains("Hужно"));
    search.setCharset("CP866");
    CHECK(search.contains("Hужно"));
    CHECK(search.query() == "Нужно");
    search.setCharset("KOI8-R");
    CHECK_FALSE(search.contains("Hужно"));
}

TEST_CASE("Bytes that are no UTF-8 stand for themselves", "[text_search]") {
    // A message stating a charset iconv does not know comes through with its
    // bytes untouched. Nothing may match across them by accident, and nothing
    // may spin.
    const TextSearch search("ab", "CP437");
    CHECK(
        search.contains(std::string("\xC3"
                                    "ab"
                                    "\xFF")));
    CHECK_FALSE(TextSearch(std::string("\xFE"), "CP437").contains(std::string("\xFF")));
}
