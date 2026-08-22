#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "config/app_config.hpp"
#include "domain/area.hpp"
#include "domain/message.hpp"

namespace amberedit::app {

/// The header of a message being written, as the editor holds it.
///
/// Text rather than parsed values, addresses included: these are the fields a
/// user types into, and what is typed is what has to be shown back. Parsing
/// happens where an address is needed — when the destination decides which AKA
/// the message goes out under.
struct ComposeFields {
    /// Whether the message is addressed to a node. Only netmail has a To
    /// address; in echomail the field addresses nobody and is not shown.
    bool netmail{false};
    /// Whether this answers a message rather than starting one. The screens
    /// only use it to say which they are doing.
    bool reply{false};
    /// Whether the reply is being written into an area other than the one the
    /// message being answered was read in — what the reader's `n` asks for.
    /// The area itself is not here: these are the fields of the header, and
    /// where the message goes is the screens' business.
    ///
    /// It is false again as soon as the next message is begun, prefill
    /// replacing the whole struct — nothing has to put it back.
    bool moved{false};
    /// Whether that move was the message's own doing rather than the user's:
    /// `areareplydirect` following the `AREA:` line the message begins with.
    /// Always with `moved`, which is what carries the answer into the other
    /// area — this only says who decided.
    ///
    /// Nothing was moved as far as the message is concerned: it is being
    /// answered in the echo it says it was posted to, which is where an answer
    /// belongs. So the template's @moved lines are held off it — they say
    /// "answering a msg posted in area X" about an area the answer is *not*
    /// going into, and here that area is the collector the message was read in,
    /// which is nothing the network needs to be told about.
    bool direct{false};
    /// Whether the message is one already in the base being written again,
    /// rather than a new one — the reader's `change`. It is the same message
    /// afterwards: it keeps its place and its number, it is dated by the hour
    /// it was last written in, and what is edited is what a person can see of
    /// it.
    ///
    /// It is what the template's @changed lines are turned on by, and what
    /// stops the editor from expanding a template over the message at all: the
    /// text it opens on is the message itself.
    bool changing{false};
    /// Whether the message passes another one on rather than starting or
    /// answering anything — the reader's `m`. It is a new message in every
    /// other respect: addressed to the area rather than to whoever wrote the
    /// one being forwarded, and carrying no reply link back to it. What it adds
    /// is the template's @forward lines and the original's text where @message
    /// stands.
    bool forward{false};

    /// The FTS-0001 attribute bits the message goes out with — the ones
    /// `domain::messageAttributes()` names when it is read back, set on the header
    /// screen with Ctrl and a letter. The prefill seeds them: everything
    /// written here is local, and netmail is private.
    uint32_t attributes{0};

    std::string fromName;
    std::string fromAddr;
    std::string toName;
    std::string toAddr;
    std::string subject;
};

/// The address this system is known by in `area`: the AKA the tosser presents
/// it under, or the config's own `address` where the tosser named none — which
/// AreaManager has already filled in, so this only has to cope with a config
/// that names no address either, and answers empty for one.
///
/// It is what a message written in the area is written from, and what a message
/// changed there is stamped by: the MSGID of a rewritten message names the
/// system that made it, which is this one.
[[nodiscard]] std::string ownAddress(const config::AppConfig& config,
                                     const domain::AreaConfig& area);

/// The fields a new message in `area` starts with.
///
/// The sender is the AKA the area is presented under — the tosser's, since
/// AreaManager has already put the config's own address in where the tosser
/// stated none. Echomail is addressed to All, as it has always been; netmail
/// is addressed to nobody until the user says who.
ComposeFields newMessage(const config::AppConfig& config, const domain::AreaConfig& area);

/// The fields a reply to `header` starts with: `readIn` is the area it was read
/// in and `into` the one the answer is written into — the same area for a plain
/// reply, and two of them where the answer is moved elsewhere.
///
/// The subject is carried over unchanged, which is what FTN readers have always
/// done — a reply is part of the same thread and says so by its subject.
///
/// A netmail reply goes out from the address the message came to, so that a
/// correspondent sees the same AKA answering that they wrote to — but only when
/// the message was written to an address at all, and that address is one of
/// ours. An echo was written to the area rather than to anybody, and a netmail
/// that reached us addressed to somebody else (a routed one, an area shared
/// with another point) would have us forging their address. Where there is no
/// AKA of ours to keep, the recipient chooses one the way `senderFor()` does,
/// and `into`'s own AKA stands where no [akamatch] rule covers them.
ComposeFields reply(const config::AppConfig& config, const domain::AreaConfig& readIn,
                    const domain::AreaConfig& into, const domain::MessageHeader& header);

/// The fields a comment on `header` starts with: the reply above in every
/// respect — the subject carried over, the quote, the reply link, the sender's
/// AKA chosen the same way — save that it is addressed to whoever the message
/// was written *to* rather than to whoever wrote it.
///
/// It is how a third party picks up what was said to somebody else, and how a
/// message of one's own read back is carried on to the same correspondent. In
/// netmail the whole of the To row comes from the recipient, name and address
/// together; in echomail there is no address to take, as there is none in a
/// reply.
///
/// The sender is left exactly as `reply()` chose it, which is a rule about the
/// message being answered and not about who is being written to: a netmail that
/// was addressed to us is answered from that AKA, and a comment on it is then
/// written from and to the same address — which is what a comment on one's own
/// message is.
ComposeFields commentReply(const config::AppConfig& config,
                           const domain::AreaConfig& readIn,
                           const domain::AreaConfig& into,
                           const domain::MessageHeader& header);

/// The fields a message being changed starts with: its own, as the base holds
/// them.
///
/// Nothing is prefilled and nothing is chosen — not the sender, which is
/// whoever wrote the message and not whoever is changing it. A change begins
/// from the message as it stands; the editor is where any of it is decided
/// otherwise.
///
/// The one attribute that does not come across is `MSGSENT`: what went out is not
/// what is being written now, so a message that had gone out is unsent again.
ComposeFields change(const domain::AreaConfig& area, const domain::MessageHeader& header);

/// The AKA a netmail to `dest` should go out under, once the destination is
/// known, or nothing when no [akamatch] rule covers it — in which case the
/// sender stays as newMessage()/reply() left it.
std::optional<std::string> senderFor(const config::AppConfig& config,
                                     const domain::FtnAddress& dest);

}  // namespace amberedit::app
