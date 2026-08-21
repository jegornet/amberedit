#include <catch2/catch.hpp>

#include <string>

#include "ui/text_editor.hpp"

using amberedit::ui::deleteAt;
using amberedit::ui::deleteBefore;
using amberedit::ui::deleteLine;
using amberedit::ui::EditOptions;
using amberedit::ui::insertNewline;
using amberedit::ui::insertText;
using amberedit::ui::TextBuffer;
using amberedit::ui::trimmedLines;

namespace {

/// The buffer as one string, lines separated by '|' and the cursor shown as
/// '^' — everything a failure needs to say in one line.
std::string shown(const TextBuffer& buffer) {
    std::string out;
    for (size_t i = 0; i < buffer.lines.size(); ++i) {
        if (i != 0) out += '|';
        const std::string& line = buffer.lines[i];
        if (static_cast<int>(i) == buffer.row) {
            out += line.substr(0, buffer.col) + "^" + line.substr(buffer.col);
        } else {
            out += line;
        }
    }
    return out;
}

TextBuffer bufferOf(std::vector<std::string> lines, int row, size_t col) {
    TextBuffer buffer;
    buffer.lines = std::move(lines);
    buffer.row = row;
    buffer.col = col;
    return buffer;
}

}  // namespace

TEST_CASE("Typing inserts at the cursor", "[editor]") {
    TextBuffer buffer;
    const EditOptions options{78};

    insertText(buffer, "a", options);
    insertText(buffer, "b", options);
    CHECK(shown(buffer) == "ab^");

    // A Cyrillic letter arrives as one event of two bytes and is one character
    // to everything that follows.
    insertText(buffer, "ё", options);
    CHECK(buffer.line() == "abё");
    deleteBefore(buffer);
    CHECK(shown(buffer) == "ab^");
}

TEST_CASE("A line the user writes is not wrapped at the margin", "[editor]") {
    // The margin is the quote margin: it says how quoted text is laid out and
    // nothing about how long a line of one's own may be.
    TextBuffer buffer = bufferOf({"aaa bbb ccc"}, 0, 11);
    const EditOptions options{11};

    insertText(buffer, " ", options);
    insertText(buffer, "d", options);
    CHECK(shown(buffer) == "aaa bbb ccc d^");
}

TEST_CASE("A quoted line wraps under its own prefix", "[editor]") {
    // What wraps under " AB> " has to come out as " AB> " as well, or the
    // second half of a quoted sentence reads as the answer to it.
    TextBuffer buffer = bufferOf({" AB> aaa bbb ccc"}, 0, 16);
    const EditOptions options{16};

    insertText(buffer, " ", options);
    insertText(buffer, "d", options);
    CHECK(shown(buffer) == " AB> aaa bbb ccc| AB> d^");
}

TEST_CASE("A quoted word with nowhere to break is cut at the margin", "[editor]") {
    TextBuffer buffer = bufferOf({"A> aaaaaaa"}, 0, 10);
    const EditOptions options{7};

    insertText(buffer, "b", options);
    CHECK(shown(buffer) == "A> aaaa|A> aaab^");
}

TEST_CASE("Enter carries the quote prefix onto the new line", "[editor]") {
    TextBuffer buffer = bufferOf({" AB> hello world"}, 0, 10);
    insertNewline(buffer);
    CHECK(shown(buffer) == " AB> hello| AB> ^ world");

    // An ordinary line splits without one.
    TextBuffer plain = bufferOf({"hello world"}, 0, 5);
    insertNewline(plain);
    CHECK(shown(plain) == "hello|^ world");
}

TEST_CASE("Enter at the start of a quote puts a line above it", "[editor]") {
    // The tail carries the prefix already; a second one would quote the quote.
    TextBuffer buffer = bufferOf({" AB> hello"}, 0, 0);
    insertNewline(buffer);
    CHECK(shown(buffer) == "|^ AB> hello");

    // The same anywhere inside the prefix, and one is added again as soon as
    // the split falls past it.
    TextBuffer inside = bufferOf({" AB> hello"}, 0, 3);
    insertNewline(inside);
    CHECK(shown(inside) == " AB|^> hello");

    TextBuffer after = bufferOf({" AB> hello"}, 0, 5);
    insertNewline(after);
    CHECK(shown(after) == " AB> | AB> ^hello");
}

TEST_CASE("Enter at the end of a quote leaves the new line empty", "[editor]") {
    // Nothing is split off, so there is nothing for the prefix to carry: the
    // answer to the quote starts on a line of its own.
    TextBuffer buffer = bufferOf({" PL> but it conflicts"}, 0, 21);
    insertNewline(buffer);
    CHECK(shown(buffer) == " PL> but it conflicts|^");

    // A quote prefix with nothing after it is an end of line like any other.
    TextBuffer bare = bufferOf({" PL> "}, 0, 5);
    insertNewline(bare);
    CHECK(shown(bare) == " PL> |^");

    // An ordinary line at its end splits the same way it always did.
    TextBuffer plain = bufferOf({"hello"}, 0, 5);
    insertNewline(plain);
    CHECK(shown(plain) == "hello|^");
}

TEST_CASE("Backspace and Delete join lines at their ends", "[editor]") {
    TextBuffer buffer = bufferOf({"one", "two"}, 1, 0);
    deleteBefore(buffer);
    CHECK(shown(buffer) == "one^two");

    TextBuffer other = bufferOf({"one", "two"}, 0, 3);
    deleteAt(other);
    CHECK(shown(other) == "one^two");

    // Nothing to join at the very ends of the text.
    TextBuffer first = bufferOf({"one"}, 0, 0);
    deleteBefore(first);
    CHECK(shown(first) == "^one");
}

TEST_CASE("Ctrl-Y takes the whole line", "[editor]") {
    TextBuffer buffer = bufferOf({"one", "two", "three"}, 1, 2);
    deleteLine(buffer);
    CHECK(shown(buffer) == "one|^three");

    // The last line left is emptied rather than removed: a buffer always has
    // a line for the cursor to stand on.
    TextBuffer single = bufferOf({"only"}, 0, 2);
    deleteLine(single);
    CHECK(shown(single) == "^");
}

TEST_CASE("Ctrl-W takes the word before the cursor", "[editor]") {
    using amberedit::ui::deleteWordBefore;

    TextBuffer buffer = bufferOf({"one two three"}, 0, 13);
    deleteWordBefore(buffer);
    CHECK(shown(buffer) == "one two ^");
    // The separators go with the word rather than being left behind one at a
    // time: what is erased is where Alt+B would have gone back to.
    deleteWordBefore(buffer);
    CHECK(shown(buffer) == "one ^");
    deleteWordBefore(buffer);
    CHECK(shown(buffer) == "^");
    deleteWordBefore(buffer);
    CHECK(shown(buffer) == "^");

    // From inside a word, only what stands before the cursor.
    TextBuffer inside = bufferOf({"one two"}, 0, 5);
    deleteWordBefore(inside);
    CHECK(shown(inside) == "one ^wo");

    // Within the line and no further: at the start of one there is no word
    // before the cursor, and the line break is Backspace's to take out.
    TextBuffer joined = bufferOf({"one", "two"}, 1, 0);
    deleteWordBefore(joined);
    CHECK(shown(joined) == "one|^two");

    // A word is a word whatever alphabet it is in, and the whole character
    // goes with it.
    TextBuffer cyrillic =
        bufferOf({"AB> Привет, мир"}, 0, std::string("AB> Привет,").size());
    deleteWordBefore(cyrillic);
    CHECK(shown(cyrillic) == "AB> ^ мир");
}

TEST_CASE("What is saved has no blank lines trailing it", "[editor]") {
    const TextBuffer buffer = bufferOf({"text", "", "  ", ""}, 0, 0);
    const auto lines = trimmedLines(buffer);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "text");
}

TEST_CASE("Alt+F and Alt+B move a word at a time", "[editor]") {
    using amberedit::ui::moveWordLeft;
    using amberedit::ui::moveWordRight;

    TextBuffer buffer = bufferOf({"one two three"}, 0, 0);
    moveWordRight(buffer);
    CHECK(shown(buffer) == "one^ two three");
    moveWordRight(buffer);
    CHECK(shown(buffer) == "one two^ three");
    moveWordRight(buffer);
    CHECK(shown(buffer) == "one two three^");
    // Nowhere further to go.
    moveWordRight(buffer);
    CHECK(shown(buffer) == "one two three^");

    moveWordLeft(buffer);
    CHECK(shown(buffer) == "one two ^three");
    moveWordLeft(buffer);
    CHECK(shown(buffer) == "one ^two three");
    moveWordLeft(buffer);
    CHECK(shown(buffer) == "^one two three");
    moveWordLeft(buffer);
    CHECK(shown(buffer) == "^one two three");
}

TEST_CASE("A word is what is left once the punctuation is stepped over", "[editor]") {
    using amberedit::ui::moveWordLeft;
    using amberedit::ui::moveWordRight;

    // The markers and spaces of a quote prefix are punctuation like any other.
    TextBuffer buffer = bufferOf({" AB> Привет, мир"}, 0, 0);
    moveWordRight(buffer);
    CHECK(shown(buffer) == " AB^> Привет, мир");
    moveWordRight(buffer);
    CHECK(shown(buffer) == " AB> Привет^, мир");
    moveWordRight(buffer);
    CHECK(shown(buffer) == " AB> Привет, мир^");

    // Backwards it lands at the start of the word, Cyrillic or not.
    moveWordLeft(buffer);
    CHECK(shown(buffer) == " AB> Привет, ^мир");
}

TEST_CASE("A word move carries on to the next line", "[editor]") {
    using amberedit::ui::moveWordLeft;
    using amberedit::ui::moveWordRight;

    TextBuffer buffer = bufferOf({"one", "", "two"}, 0, 3);
    moveWordRight(buffer);
    CHECK(shown(buffer) == "one||two^");

    moveWordLeft(buffer);
    CHECK(shown(buffer) == "one||^two");
    moveWordLeft(buffer);
    CHECK(shown(buffer) == "^one||two");
}

TEST_CASE("Ctrl-D takes out the quote the cursor stands in", "[editor]") {
    using amberedit::ui::deleteQuote;

    // Down to the first line that is neither a quote nor blank, which stays.
    TextBuffer buffer =
        bufferOf({"they wrote:", " AB> one", " AB>", " AB> two", "", "my answer"}, 1, 3);
    deleteQuote(buffer);
    CHECK(shown(buffer) == "they wrote:|^my answer");

    // The blank lines inside the block go with it, and so do the ones after.
    TextBuffer trailing = bufferOf({" AB> one", "", "", "--- AmberEdit"}, 0, 0);
    deleteQuote(trailing);
    CHECK(shown(trailing) == "^--- AmberEdit");
}

TEST_CASE("Ctrl-D on an answer does nothing", "[editor]") {
    using amberedit::ui::deleteQuote;

    // There is no block to take out here, and taking the answer out instead of
    // the quote is the one mistake worth avoiding.
    TextBuffer buffer = bufferOf({" AB> one", "my answer", " AB> two"}, 1, 2);
    deleteQuote(buffer);
    CHECK(shown(buffer) == " AB> one|my^ answer| AB> two");
}

TEST_CASE("Ctrl-D on the last quote leaves a line to write on", "[editor]") {
    using amberedit::ui::deleteQuote;

    TextBuffer buffer = bufferOf({" AB> one", " AB> two"}, 0, 0);
    deleteQuote(buffer);
    CHECK(shown(buffer) == "^");
}
