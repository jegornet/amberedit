#include <catch2/catch.hpp>

#include <string>

#include "app/msg_template.hpp"

using amberedit::app::expandTemplate;
using amberedit::app::TemplateContext;

namespace {

TemplateContext replyContext() {
    TemplateContext context;
    context.cname = "Yegor Gluhov";
    context.caddr = "192:168/2";
    context.cdate = "11 Aug 26";
    context.ctime = "20:15:00";
    context.cecho = "test.echo";
    // Where the message being answered was read, which for a reply that stays
    // in its area is the area being written in.
    context.oecho = "test.echo";
    context.oname = "Vasya Pupkin";
    context.oaddr = "192:168/3.1";
    context.odate = "10 Aug 26";
    context.otime = "21:19:36";
    context.omsgid = "192:168/3.1 68a1b2c3";
    context.tname = "Vasya Pupkin";
    context.taddr = "192:168/3.1";
    // The message being answered was written to us, which is what the `d*`
    // tokens name.
    context.dname = "Yegor Gluhov";
    context.daddr = "192:168/2";
    context.subject = "test";
    context.origin = "AmberEdit test";
    context.tearline = "AmberEdit";
    context.areaname = "test.echo";
    context.isReply = true;
    context.isQuoted = true;
    context.isEcho = true;
    context.quote = {" VP> hello", " VP> world"};
    return context;
}

/// The expansion as one string, lines separated by '|'.
std::string expanded(const std::string& text, const TemplateContext& context) {
    std::string out;
    for (const auto& line : expandTemplate(text, context).lines) {
        if (!out.empty()) out += '|';
        out += line;
    }
    return out;
}

}  // namespace

TEST_CASE("A template's comments and conditionals decide what appears", "[template]") {
    const auto context = replyContext();

    CHECK(expanded("; a comment\nplain", context) == "plain");
    CHECK(expanded("@newnew message", context).empty());
    CHECK(expanded("@quotedquoting", context) == "quoting");
    // A reply that quotes is not a @reply line, which is for replies without
    // a quote.
    CHECK(expanded("@replyno quote", context).empty());
    CHECK(expanded("@echoin an echo", context) == "in an echo");
    CHECK(expanded("@netin netmail", context).empty());
    // Conditionals stack on one line.
    CHECK(expanded("@quoted@echoboth", context) == "both");
    // A reply is not a message being changed.
    CHECK(expanded("@changedchanged", context).empty());
    // A reply is not a forward, whatever else it is.
    CHECK(expanded("@forwardforwarded", context).empty());
    // A reply answered where it was read is not a moved one.
    CHECK(expanded("@movedmoved", context).empty());
}

TEST_CASE("The @Changed lines are taken out of a template on their own", "[template]") {
    // What a message being changed gets: those lines and nothing else of the
    // template, since the editor opens on the message itself rather than on a
    // template with the message inside it.
    auto context = replyContext();
    context.isReply = false;
    context.isQuoted = false;
    context.isChanged = true;

    const std::string tpl =
        "; a comment\n"
        "@Moved*** Answering a msg posted in area @OEcho\n"
        "@Changed\n"
        "@Changed*** Changed by @CName (@CAddr), @CDate @CTime\n"
        "@Changed\n"
        "Hello @pseudo!\n"
        "@Quote\n";
    const auto lines = amberedit::app::conditionalLines(tpl, "changed", context);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].empty());
    CHECK(lines[1] == "*** Changed by Yegor Gluhov (192:168/2), 11 Aug 26 20:15:00");
    CHECK(lines[2].empty());

    // A line carrying another conditional that does not hold is left out, as it
    // would be in a whole expansion.
    CHECK(amberedit::app::conditionalLines("@Changed@netonly in netmail", "changed",
                                           context)
              .empty());
    CHECK(amberedit::app::conditionalLines("@Changed@echoin an echo", "changed", context)
              .size() == 1);
    // And a template that names none gives none.
    CHECK(amberedit::app::conditionalLines("Hello @pseudo!", "changed", context).empty());
}

TEST_CASE("A forward puts the message in where @message stands", "[template]") {
    auto context = replyContext();
    context.isReply = false;
    context.isQuoted = false;
    context.isNew = true;
    context.isForward = true;
    context.quote.clear();
    context.message = {"hello", "world"};

    CHECK(expanded("@forward* Area : @OEcho\n@message\n@forward*", context) ==
          "* Area : test.echo|hello|world|*");
    // @message is not a conditional: it stands unprefixed in the template
    // GoldED ships, and what keeps it out of a reply is having nothing to put
    // in — the quote goes in through @quote instead.
    auto reply = replyContext();
    CHECK(expanded("before\n@message\nafter", reply) == "before|after");
    CHECK(expanded("@quote", reply) == " VP> hello| VP> world");
}

TEST_CASE("A moved reply names the area it was answering", "[template]") {
    auto context = replyContext();
    context.isMoved = true;
    // Where it is being written, and where the message it answers was read.
    context.cecho = "test.other";
    context.oecho = "test.echo";

    CHECK(expanded("@moved*** Answering a msg posted in area @OEcho.", context) ==
          "*** Answering a msg posted in area test.echo.");
    // The two are the same area for a reply that stays where it was, and the
    // token is the one a template written for GoldED already uses.
    CHECK(expanded("@OEcho", replyContext()) == "test.echo");
}

TEST_CASE("A template substitutes what it names", "[template]") {
    const auto context = replyContext();

    // @tname is who this message is going to; @dname is who the one being
    // answered went to. GoldED's own template writes the second in its
    // attribution line, and reading it as the first would have every reply say
    // that its author wrote to himself.
    CHECK(expanded("@odate @otime, @oname wrote to @dname:", context) ==
          "10 Aug 26 21:19:36, Vasya Pupkin wrote to Yegor Gluhov:");
    CHECK(expanded("to @tname", context) == "to Vasya Pupkin");
    CHECK(expanded("@cname (@caddr) @cdate", context) ==
          "Yegor Gluhov (192:168/2) 11 Aug 26");
    CHECK(expanded("@ofname @olname @cfname", context) == "Vasya Pupkin Yegor");
    CHECK(expanded("@omsgid", context) == "192:168/3.1 68a1b2c3");
    CHECK(expanded("@subject in @areaname", context) == "test in test.echo");

    // The longest token wins: @cdate is not @c followed by "date".
    CHECK(expanded("@cdate|@caddr|@cname", context) ==
          "11 Aug 26|192:168/2|Yegor Gluhov");

    // "@@" is how a template writes a literal '@'.
    CHECK(expanded("somebody@@veryhot.com", context) == "somebody@veryhot.com");
    // What is not a token is left as it was written.
    CHECK(expanded("@nosuchtoken", context) == "@nosuchtoken");
}

TEST_CASE("Name tokens take their {mine}{theirs} parameters", "[template]") {
    auto context = replyContext();

    CHECK(expanded("@oname{I}{you} wrote to @dname{me}{you}:", context) ==
          "you wrote to me:");

    // The message being answered is mine, so the first parameter applies.
    context.oname = "Yegor Gluhov";
    CHECK(expanded("@oname{I}{you} wrote:", context) == "I wrote:");

    // The third parameter is for an echo addressed to everybody.
    context.dname = "All";
    CHECK(expanded("to @dname{me}{you}{everyone}:", context) == "to everyone:");
}

TEST_CASE("A third party keeps their name where {mine}{theirs} does not fit",
          "[template]") {
    // Answering a message somebody else wrote to a third person. The two
    // parameters are mine and my opponent's — the one I am writing to — and
    // Nil A is neither, so the name stands as it is rather than coming out
    // "you wrote to you".
    auto context = replyContext();
    context.oname = "Stas Mishchenkov";
    context.tname = "Stas Mishchenkov";
    context.dname = "Nil A";
    CHECK(expanded("@oname{I}{you} wrote to @dname{me}{you}:", context) ==
          "you wrote to Nil A:");

    // The same message written to me: then @dname is mine.
    context.dname = "Yegor Gluhov";
    CHECK(expanded("@oname{I}{you} wrote to @dname{me}{you}:", context) ==
          "you wrote to me:");

    // Answering a message of my own: I wrote it, and whoever I wrote it to is
    // still a third party — the reply goes back to me.
    context.oname = "Yegor Gluhov";
    context.tname = "Yegor Gluhov";
    context.dname = "Brother Rabbit";
    CHECK(expanded("@oname{I}{you} wrote to @dname{me}{you}:", context) ==
          "I wrote to Brother Rabbit:");

    // Whose name it is decides which parameter applies, whatever part of the
    // name the token itself writes.
    context = replyContext();
    CHECK(expanded("@ofname{I}{you} and @dfname{me}{you} and @cfname{I}{you}",
                   context) == "you and me and I");
    // A name is matched whole: a namesake's first name is not mine.
    context.dname = "Yegor Ivanov";
    CHECK(expanded("@dname{me}{you}", context) == "Yegor Ivanov");

    // @areaname is no name of a person, and eats no braces.
    CHECK(expanded("@areaname{x}", context) == "test.echo{x}");
}

TEST_CASE("Insert tokens replace the line they stand on", "[template]") {
    const auto context = replyContext();

    CHECK(expanded("before\n@quote ignored text\nafter", context) ==
          "before| VP> hello| VP> world|after");
    // @message inserts the original in full when forwarding or changing one,
    // and AmberEdit does neither — so the line GoldED's own template keeps between
    // two @forward lines inserts nothing. Inserting it would put the original
    // in front of the quote of the same message.
    CHECK(expanded("@message", context).empty());
    // A cookie file and message attributes mean nothing here either; the line
    // goes either way, since an insert token takes the whole line with it.
    CHECK(expanded("@random\n@attrib CRA", context).empty());
}

TEST_CASE("A template says where the cursor starts and what the header holds",
          "[template]") {
    const auto context = replyContext();

    const auto result =
        expandTemplate("@quoted@odate, @oname wrote:\n@quoted@position\n@quote", context);
    REQUIRE(result.lines.size() == 4);
    CHECK(result.lines[0] == "10 Aug 26, Vasya Pupkin wrote:");
    CHECK(result.lines[1].empty());
    CHECK(result.cursorLine == 1);

    const auto forced =
        expandTemplate("@forcesubj \"Re: something\"\n@setto \"Sysop\"", context);
    CHECK(forced.setSubject == "Re: something");
    CHECK(forced.forceSubject);
    CHECK(forced.setTo == "Sysop");
    CHECK_FALSE(forced.forceTo);
    CHECK(forced.lines.empty());
}
