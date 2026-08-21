#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace amberedit::app {

/// Everything a message template can ask about, ready to be written into it.
///
/// The names follow the tokens they answer (see TEMPLATE.md): `c` is the
/// current user, `o` the message being answered, `t`/`d` the recipient.
/// Whatever a template asks for that AmberEdit does not know is empty rather than
/// absent — a template written for another reader should still produce a
/// message, not an error.
struct TemplateContext {
    // The current user and system.
    std::string cname;
    std::string caddr;
    std::string c3daddr;
    std::string cdate;
    std::string ctime;
    std::string ctzoffset;
    std::string cecho;
    std::string cdesc;

    // The message being answered, empty for a new one.
    std::string oname;
    std::string oaddr;
    std::string o3daddr;
    std::string odate;
    std::string otime;
    std::string otzoffset;
    std::string omsgid;
    /// The area the message being answered was read in. The same as `cecho` and
    /// `cdesc` for an ordinary reply, and the area left behind for one moved
    /// into another — which is the whole of what @moved has to say.
    std::string oecho;
    std::string odesc;

    // Who this message is being written to — the `t*` tokens.
    std::string tname;
    std::string taddr;
    std::string t3daddr;

    // Who the message being answered was written to — the `d*` tokens.
    ///
    /// Not the same as the `t*` ones, however alike the names look. GoldED's
    /// own template writes "@oname{I}{you} wrote to @dname{me}{you}", and the
    /// {me} there can only ever come out when @dname is the recipient of the
    /// message being answered — which in a reply is usually us. For a new
    /// message there is nothing being answered, and these fall back to the
    /// recipient of the message being written.
    std::string dname;
    std::string daddr;
    std::string d3daddr;

    std::string subject;
    std::string origin;
    std::string tearline;
    std::string tagline;
    std::string version;
    std::string pid;
    std::string longpid;

    std::string areaname;
    std::string areapath;
    std::string areatype;

    /// Which of the conditional tokens hold.
    bool isNew{false};
    bool isReply{false};    ///< a reply, quoted or not
    bool isQuoted{false};   ///< a reply that quotes
    bool isMoved{false};    ///< a reply written into another area than it answers
    bool isForward{false};  ///< another message passed on rather than answered
    bool isChanged{false};  ///< a message already in the base, being rewritten
    bool isEcho{false};
    bool isNet{false};
    bool isLocal{false};

    /// What @quote puts in: the message being answered, quoted.
    std::vector<std::string> quote;

    /// What @message puts in: the message being forwarded, as it was written.
    /// No quote prefix — a forward passes the message on rather than answering
    /// it, and the @forward lines around it are what say whose it is.
    ///
    /// Empty for everything else, and then the line the token stands on simply
    /// goes — which is what the template GoldED ships expects, @message
    /// standing on a line of its own between two @forward lines. A message
    /// being changed does not fill it either: the editor opens on the message
    /// itself rather than on a template with the message inside it.
    std::vector<std::string> message;

    /// Where a relative @include path is looked for — the template's own
    /// directory, as every other reader resolves them.
    std::string includeDir;
};

/// A template expanded into the lines a message starts from.
struct TemplateResult {
    std::vector<std::string> lines;
    /// The line @position asked the cursor to start on, or -1 when the
    /// template named none and it belongs at the top.
    int cursorLine{-1};
    /// What @setsubj / @forcesubj and their relatives asked for. Empty means
    /// the template said nothing and the header stands as edited.
    std::string setFrom;
    std::string setTo;
    std::string setSubject;
    bool forceFrom{false};
    bool forceTo{false};
    bool forceSubject{false};
};

/// Expands a template's text against a context, by the rules in TEMPLATE.md:
/// a leading ';' comments a line out, a leading conditional token decides
/// whether the line appears at all, insert tokens replace the line with what
/// they name, and everything else is substituted where it stands. `@@` is a
/// literal '@'; an unknown token is left as it was written.
[[nodiscard]] TemplateResult expandTemplate(const std::string& text,
                                            const TemplateContext& context);

/// The lines a template marks with one named conditional — `@Changed` and the
/// rest — expanded against the context, and nothing else of the template.
///
/// It is what a message that is *only* those lines is built from: changing a
/// message opens the editor on the message itself, not on a template with the
/// message somewhere inside it, and the notice saying whose hand it was in
/// stands at the head of it. A line carrying another conditional that does not
/// hold is left out, as it would be in a whole expansion.
[[nodiscard]] std::vector<std::string> conditionalLines(const std::string& text,
                                                        std::string_view condition,
                                                        const TemplateContext& context);

/// The @tokens of one line replaced, and nothing else: no comment rule, no
/// conditionals, no insert tokens. It is what a config line asks for —
/// "@longpid @version" in the tearline is one line of text with tokens in it,
/// not a template — and the tokens mean there exactly what they mean in one.
[[nodiscard]] std::string expandTokens(std::string_view line,
                                       const TemplateContext& context);

}  // namespace amberedit::app
