#pragma once

#include <cstdint>
#include <string>

#include "domain/area.hpp"
#include "domain/message.hpp"
#include "support/result.hpp"

namespace amberedit::ports {

/// Access to one area's message base. Implementations are responsible for
/// converting to UTF-8: above this port single-byte encodings do not exist.
///
/// Messages are indexed 1-based, as in smapi: valid values for index lie in
/// the range [1, count()].
class IMsgBase {
public:
    virtual ~IMsgBase() = default;

    /// Opens the area's base, or says why it is unavailable — missing files, a
    /// corrupt base, an unsupported type.
    [[nodiscard]] virtual Result<void> open(const domain::AreaConfig& area) = 0;
    virtual void close() = 0;

    [[nodiscard]] virtual uint32_t count() const = 0;
    [[nodiscard]] virtual domain::MessageHeader header(uint32_t index) const = 0;
    [[nodiscard]] virtual domain::MessageBody body(uint32_t index) const = 0;

    /// What the message answers and what answers it, as message numbers.
    ///
    /// Asked for one message at a time rather than carried in every header:
    /// the links are kept as UIDs and each one costs a lookup, and only the
    /// message being read has any use for them.
    [[nodiscard]] virtual domain::MessageThread thread(uint32_t index) const = 0;

    /// The message's UID: the number that identifies it for as long as it
    /// exists, where the position shifts under every pack and renumber. This
    /// is what a lastread mark is made of. Zero means there is no such message.
    [[nodiscard]] virtual uint32_t uidOf(uint32_t index) const = 0;

    /// What the base holds about the message besides the message itself: the
    /// header as it is stored, the records naming it, and the bytes of both.
    ///
    /// This is the one thing above this port that is about the *storage* rather
    /// than about the message, and it is deliberately a report to be shown and
    /// not a structure to be acted on: nothing decides anything by it. Every
    /// format answers it its own way, and a message that cannot be read comes
    /// back empty rather than half filled in.
    [[nodiscard]] virtual domain::MessageInfo info(uint32_t index) const = 0;

    /// The other way about: the position of the message with this UID, or of
    /// the nearest earlier one when it has since been deleted — a mark left on
    /// a message that is gone still says how far the reading got. Zero when
    /// nothing at or before it survives.
    [[nodiscard]] virtual uint32_t indexOfUid(uint32_t uid) const = 0;

    /// Appends a message to the base, converting its text out of UTF-8 into
    /// the charset the draft names, and hands back its 1-based number.
    [[nodiscard]] virtual Result<uint32_t> write(const domain::MessageDraft& draft) = 0;

    /// Writes the draft over message `index`, in its place in the base rather
    /// than beside it. A failure means the message is unchanged.
    ///
    /// It stays the same message: its UID, the links tying it to the messages
    /// it answers and is answered by, and the stamp it arrived here under are
    /// all kept, and every other message keeps its number. What the draft
    /// decides is the header fields, the control lines and the text —
    /// everything a person can see and change. The date it is written under is
    /// the clock: a message written again is written now.
    [[nodiscard]] virtual Result<void> replace(uint32_t index,
                                               const domain::MessageDraft& draft) = 0;

    /// Takes the message out of the base. A failure means it is still there.
    ///
    /// Everything after it moves up one, so the number that named it now names
    /// what followed it — as in every FTN base, where a message's place is not
    /// its identity.
    [[nodiscard]] virtual Result<void> remove(uint32_t index) = 0;

    /// Writes the base's own "this has been read" mark onto message `index` —
    /// JAM's `TimesRead`, Squish's `MSGSEEN`, the `times_read` word of a Fido
    /// *.msg — which is what `domain::MessageHeader::seen` reads back.
    ///
    /// The message itself is untouched: not its text, not its attributes, not the
    /// stamp it is dated by. A message already marked is left alone and this
    /// succeeds, so opening one twice writes once. A failure means the mark was
    /// not made — an area opened read-only is the ordinary reason, and it is not
    /// worth telling anybody about: the message is on the screen either way.
    [[nodiscard]] virtual Result<void> markSeen(uint32_t index) = 0;
};

}  // namespace amberedit::ports
