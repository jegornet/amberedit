#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "app/quoting.hpp"
#include "ui/text_layout.hpp"

using amberedit::app::parseQuotePrefix;
using amberedit::app::quoteLines;
using amberedit::app::quotePrefixFor;

namespace {

/// The quoted body as one string, lines separated by '|' — easier to read in a
/// failure than a vector of strings.
std::string quoted(const std::vector<std::string>& lines, const std::string& author,
                   const std::string& quoteString, int margin) {
    std::string out;
    bool first = true;
    for (const auto& line : quoteLines(lines, author, quoteString, margin)) {
        if (!first) out += '|';
        first = false;
        out += line;
    }
    return out;
}

}  // namespace

TEST_CASE("The quote prefix is the quote string with the initials in it [quoting]") {
    // The example from the configuration: F is the first name's letter, L the
    // last name's, M every name in between.
    CHECK(quotePrefixFor(" FL> ", "The Lord of the Rings") == " TR> ");
    CHECK(quotePrefixFor(" FML> ", "The Lord of the Rings") == " TLotR> ");
    CHECK(quotePrefixFor(">", "The Lord of the Rings") == "> ");

    CHECK(quotePrefixFor(" FL> ", "Vasya Pupkin") == " VP> ");
    // One word is a first name and nothing else.
    CHECK(quotePrefixFor(" FL> ", "All") == " A> ");
    CHECK(quotePrefixFor(" FL> ", "") == " > ");

    // Initials are written in Cyrillic as often as not, and a letter there is
    // two bytes.
    CHECK(quotePrefixFor(" FL> ", "Иван Петров") == " ИП> ");

    // The space after the markers is not the quote string's to leave out.
    CHECK(quotePrefixFor(" FL>", "Vasya Pupkin") == " VP> ");
}

TEST_CASE("What we quote with is what our own reader reads back [quoting]") {
    // The prefix has to satisfy ui::quoteDepth(), or a reply would go out
    // looking like a quote to nobody, AmberEdit included.
    CHECK(amberedit::ui::quoteDepth(quotePrefixFor(" FL> ", "Vasya Pupkin") + "text") ==
          1);
    CHECK(amberedit::ui::quoteDepth(quotePrefixFor(" FML> ", "The Lord of the Rings") +
                                    "text") == 1);
    CHECK(amberedit::ui::quoteDepth(quotePrefixFor(">", "Vasya Pupkin") + "text") == 1);
}

TEST_CASE("An existing quote gains a level and keeps its initials [quoting]") {
    CHECK(quoted({" AB> hello"}, "Vasya Pupkin", " FL> ", 78) == " AB>> hello");
    CHECK(quoted({" AB>> hello"}, "Vasya Pupkin", " FL> ", 78) == " AB>>> hello");
    // The leading spaces are the quote string's, whatever the quoted line used.
    CHECK(quoted({"AB> hello"}, "Vasya Pupkin", " FL> ", 78) == " AB>> hello");
    CHECK(quoted({" AB> hello"}, "Vasya Pupkin", ">", 78) == "AB>> hello");
}

TEST_CASE("A line that is not a quote gets the author's initials [quoting]") {
    CHECK(quoted({"hello"}, "Vasya Pupkin", " FL> ", 78) == " VP> hello");
    // A '>' without the space after it is text, not a quote.
    CHECK(quoted({">8 lines follow"}, "Vasya Pupkin", " FL> ", 78) ==
          " VP> >8 lines follow");
}

TEST_CASE("A line with nothing on it comes out empty rather than quoted [quoting]") {
    // Nothing to answer, and a reply padded with empty quotes reads worse than
    // one without them — but the break in the text is the answered message's
    // paragraphing and stays.
    CHECK(quoted({""}, "Vasya Pupkin", " FL> ", 78).empty());
    CHECK(quoted({"     "}, "Vasya Pupkin", " FL> ", 78).empty());
    CHECK(quoted({"\t "}, "Vasya Pupkin", " FL> ", 78).empty());
    // A line that is a quote prefix and no more goes the same way, at whatever
    // depth and whether or not it kept its trailing space.
    CHECK(quoted({" AB> "}, "Vasya Pupkin", " FL> ", 78).empty());
    CHECK(quoted({" AB>>   "}, "Vasya Pupkin", " FL> ", 78).empty());

    // One empty line between two paragraphs stays one.
    CHECK(quoted({"one", "", "two"}, "Vasya Pupkin", " FL> ", 78) ==
          " VP> one|| VP> two");
}

TEST_CASE("A run of empty lines comes out as one [quoting]") {
    // Whatever they were written as — bare, spaces, or empty quotes — several
    // in a row are one break in the text.
    CHECK(quoted({"one", "", "  ", " AB> ", "two"}, "Vasya Pupkin", " FL> ", 78) ==
          " VP> one|| VP> two");
    CHECK(quoted({"one", "", "", "", "two"}, "Vasya Pupkin", " FL> ", 78) ==
          " VP> one|| VP> two");

    // The head and the tail of the quote are no different: what the answered
    // message spaced its text with, the reply is written between.
    CHECK(quoted({"", "", "one"}, "Vasya Pupkin", " FL> ", 78) == "| VP> one");
    CHECK(quoted({"one", "", ""}, "Vasya Pupkin", " FL> ", 78) == " VP> one|");
}

TEST_CASE("A quoted line is wrapped at the margin, prefix and all [quoting]") {
    const std::string long_ = "aaa bbb ccc ddd eee fff";  // 23 characters
    // Prefix " VP> " is five, so a margin of 20 leaves fifteen — exactly what
    // the first four words take, and the line comes out 20 wide.
    CHECK(quoted({long_}, "Vasya Pupkin", " FL> ", 20) ==
          " VP> aaa bbb ccc ddd|"
          " VP> eee fff");

    // Every wrapped piece carries the same prefix, deepened where the source
    // was a quote already.
    CHECK(quoted({" AB> aaa bbb ccc ddd"}, "Vasya Pupkin", " FL> ", 15) ==
          " AB>> aaa bbb|"
          " AB>> ccc ddd");

    // A word with nowhere to break is cut rather than allowed past the margin.
    CHECK(quoted({"aaaaaaaaaaaaaaaaaaaa"}, "Vasya Pupkin", " FL> ", 10) ==
          " VP> aaaaa|"
          " VP> aaaaa|"
          " VP> aaaaa|"
          " VP> aaaaa");
}

TEST_CASE("A line that fits is quoted exactly as it stands [quoting]") {
    // Indentation and columns are information; only what overflows is
    // rearranged.
    CHECK(quoted({"   a   b   c"}, "Vasya Pupkin", " FL> ", 78) == " VP>    a   b   c");
}

TEST_CASE("parseQuotePrefix reads what quoteDepth recognises [quoting]") {
    CHECK(parseQuotePrefix("hello").level == 0);
    CHECK(parseQuotePrefix(">8 lines").level == 0);

    const auto simple = parseQuotePrefix(" AB> hello");
    CHECK(simple.level == 1);
    CHECK(simple.initials == "AB");
    CHECK(simple.length == 5);

    const auto deep = parseQuotePrefix("ABC>>> hello");
    CHECK(deep.level == 3);
    CHECK(deep.initials == "ABC");
    CHECK(deep.length == 7);

    const auto bare = parseQuotePrefix("> hello");
    CHECK(bare.level == 1);
    CHECK(bare.initials.empty());
    CHECK(bare.length == 2);
}

TEST_CASE("parseQuotePrefix reads the '->' of a QWK gateway [quoting]") {
    const auto gated = parseQuotePrefix("-> hello");
    CHECK(gated.level == 1);
    CHECK(gated.initials.empty());
    CHECK(gated.length == 3);  // the '-' belongs to the prefix

    CHECK(parseQuotePrefix("->hello").level == 0);
    CHECK(parseQuotePrefix("--> hello").level == 0);
}

TEST_CASE("A '->' quote deepens into a quote of our own shape [quoting]") {
    // Text off a QWK gateway comes quoted with "-> ". Answering it gains a
    // level like any other quote, and what we write back is what our own
    // reader reads as one.
    CHECK(quoted({"-> what would I need to do"}, "Vasya Pupkin", " FL> ", 78) ==
          " >> what would I need to do");
}
