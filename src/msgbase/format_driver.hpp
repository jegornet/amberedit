#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "msgbase/raw_message.hpp"
#include "support/error.hpp"

namespace amberedit::msgbase {

/// What a run of appends came to.
///
/// **Two answers rather than one because a part of a set can be in the base.**
/// A write that fails half way through leaves what went before it written — the
/// messages are in the area and nothing here could take them back out — and the
/// caller of a move has to know exactly how many, since those are the ones it
/// may now delete from the other area. The drafts go in in the order they were
/// given, so a count from the front says which.
struct WriteReport {
    /// How many of the drafts, counting from the first, are in the base.
    uint32_t written{0};
    /// Why it stopped, where it stopped short. Null when every draft went in.
    /// A run that wrote nothing at all always carries one.
    ErrorPtr failed;
};

/// One message base format, read and written as bytes.
///
/// Three implement it — Squish, JAM and Fido *.msg — and none of them knows
/// what a charset is, what a kludge means or how a message is shown. They open
/// files, count messages, hand back what is stored and put back what they are
/// given; `FtnMsgBase` is the one place above them that turns that into the
/// domain's messages.
///
/// Messages are numbered from 1 to `count()`, as everywhere in AmberEdit, and
/// the numbering is a position rather than an identity: it changes under a
/// pack and under a delete. `uidOf()` is the identity.
class FormatDriver {
public:
    virtual ~FormatDriver() = default;

    /// Opens the base at `path` — without an extension for Squish and JAM, the
    /// directory itself for Fido *.msg.
    ///
    /// @param echo        an echomail area rather than netmail. JAM marks the
    ///                    message with it, Fido *.msg reserves a number for the
    ///                    high-water mark in one, and netmail is where the
    ///                    zone and point kludges have to be written.
    /// @param defaultZone the zone a Fido *.msg header is read under, its two
    ///                    words of address carrying none. The area's own AKA,
    ///                    where the tosser config states one.
    [[nodiscard]] virtual tl::expected<void, ErrorPtr> open(const std::string& path,
                                                            bool echo,
                                                            uint16_t defaultZone) = 0;
    virtual void close() = 0;

    /// Creates an empty base at `path`: the files the format opens by reading,
    /// holding a header that says "no messages" and nothing after it.
    ///
    /// It is how an area the tosser config declares but that nothing has yet
    /// written into comes into being. A base is otherwise made by the first
    /// tosser run, and until that has happened the area cannot be entered at
    /// all — not even to write the first message into it.
    ///
    /// The driver is not left open on what it made: creating a base and reading
    /// one are two steps on purpose, so that what is opened afterwards is the
    /// base as it stands on disk and not as this call imagined it.
    ///
    /// Nothing that is already there is written over — every file is created
    /// exclusively — and a creation that fails half way takes back what it had
    /// already made, so there is nothing left for the next attempt to trip
    /// over.
    [[nodiscard]] virtual tl::expected<void, ErrorPtr> create(
        const std::string& path) = 0;

    [[nodiscard]] virtual uint32_t count() const = 0;

    /// Reads message `index`. `withText` false stops at the header and the
    /// control lines, which is all a message list needs and, in every format,
    /// a good deal less to read.
    [[nodiscard]] virtual tl::expected<void, ErrorPtr> read(uint32_t index,
                                                            RawMessage& out,
                                                            bool withText) const = 0;

    /// What the format holds about message `index`: the stored header, the
    /// records around it and the bytes they are made of, as a report to be
    /// shown. Each format answers with its own fields — there is nothing in
    /// common between a Squish frame and a JAM subfield — and a message that
    /// cannot be read comes back empty.
    ///
    /// The text in it (names, subjects) is in the message's own charset like
    /// everything else a driver hands back, and is marked as such so that
    /// `FtnMsgBase` can convert exactly those values and leave the numbers
    /// alone.
    [[nodiscard]] virtual domain::MessageInfo info(uint32_t index) const = 0;

    /// The UID of a position, and the position of a UID. `exact` false asks for
    /// the nearest earlier message instead of nothing when the UID names one
    /// that has since been deleted — what a lastread mark wants. Zero means
    /// there is no such message.
    [[nodiscard]] virtual uint32_t uidOf(uint32_t index) const = 0;
    [[nodiscard]] virtual uint32_t indexOfUid(uint32_t uid, bool exact) const = 0;

    /// Appends a message and hands back its number.
    ///
    /// One message is the set of one: what every driver implements is
    /// `writeAll()`, and a single write is that call with one draft in it. The
    /// number is `count()` afterwards, every format appending at the end.
    ///
    /// **Every write takes the base's lock first and gives it back after**, the
    /// whole of it under `FileLock`: a tosser may be writing the same area
    /// between two keystrokes.
    [[nodiscard]] tl::expected<uint32_t, ErrorPtr> write(const RawDraft& draft) {
        WriteReport report = writeAll(std::vector<RawDraft>{draft});
        if (report.written == 0) {
            if (report.failed) return tl::make_unexpected(std::move(report.failed));
            return failure("the base took no message");
        }
        return count();
    }

    /// Appends several messages under one lock, and it is the lock's company
    /// that this exists for — `removeAll()`'s reason exactly. A write puts a few
    /// hundred bytes on the disk and re-reads the whole of what it puts them
    /// against first: JAM rebuilds its table of active messages out of the index
    /// and every header behind it, Squish reads its index back, Fido `*.msg`
    /// lists the directory. A set written one call at a time pays for that per
    /// message, so carrying a marked set into an area costs the target area over
    /// again for each message in the set. Here it is paid once.
    ///
    /// The drafts go in in the order they are given, each at the end of the
    /// base, and the base's own counters are settled once when they are all in.
    /// **The area grows by the whole set at once** as far as anything else
    /// reading it is concerned: the header that says how many messages there are
    /// is the last thing written, as it is for a single message and for the same
    /// reason.
    ///
    /// The lock is held for the length of the set, so the caller hands over a
    /// bounded number at a time — see `app::passMessages()`, which goes round in
    /// chunks and counts them on the screen.
    ///
    /// Nothing here knows about the `echotosslog`, as nothing here knows what a
    /// charset is: that file is `FtnMsgBase`'s, and which calls write a line in
    /// it is decided there.
    [[nodiscard]] virtual WriteReport writeAll(const std::vector<RawDraft>& drafts) = 0;

    /// Puts `draft` where message `index` is, rather than beside it.
    ///
    /// It is the same message afterwards: its UID, its place in the thread and
    /// the stamp it arrived here under are kept, whatever the draft says of
    /// them, and every other message keeps its number. What the draft decides
    /// is the header fields, the control lines, the text — and the stamp the
    /// message is dated by, since a message written again is written now.
    ///
    /// **The base is disturbed as little as the format allows.** A message that
    /// still fits where it lies is written there; one that has outgrown its
    /// room is written into a free frame or at the end of the file and its old
    /// room given back, and only the one record that names it changes. Nothing
    /// is copied up or down the base — a message changed at its head would cost
    /// the whole area otherwise.
    ///
    /// Takes the lock like every other write. A failure leaves the message as
    /// it was and says why.
    [[nodiscard]] virtual tl::expected<void, ErrorPtr> replace(uint32_t index,
                                                               const RawDraft& draft) = 0;

    /// Takes a message out. Everything after it moves up one.
    ///
    /// One message is the set of one: what every driver implements is
    /// `removeAll()`, and a single delete is that call with one number in it.
    [[nodiscard]] tl::expected<void, ErrorPtr> remove(uint32_t index) {
        return removeAll(std::vector<uint32_t>{index});
    }

    /// Takes several messages out under one lock, and it is the lock's company
    /// that this exists for. A delete writes a few bytes and re-reads the whole
    /// of what it writes them against first — JAM rebuilds its table of active
    /// messages out of the index and every header, Squish reads its index back —
    /// so a set taken out one call at a time pays for that re-reading once per
    /// message, and an area of any size spends the operation doing it. Here it
    /// is paid once, and what is left is the writes.
    ///
    /// The indexes are positions in the base as it stands, in any order and each
    /// named once; nothing renumbers until the call comes back. A number that is
    /// not a message stops the call before anything is written, so a set with a
    /// stale one in it takes nothing out — the numbers were worked out before
    /// the lock was taken, and a tosser packing the area meanwhile is exactly
    /// the case this refuses.
    ///
    /// A failure part way through leaves what it had already taken out taken
    /// out: there is nothing here that could put a message back, and the
    /// driver's own table is left saying what the base now holds.
    ///
    /// **Every driver takes the lock once and holds it over the whole set.** A
    /// run of a hundred thousand is minutes of the base being unwritable by
    /// anything else, which is the price of it being written correctly at all —
    /// and the caller is expected to hand over a bounded set at a time for that
    /// reason. See `message_read::removeUids()`, which is what does.
    [[nodiscard]] virtual tl::expected<void, ErrorPtr> removeAll(
        const std::vector<uint32_t>& indexes) = 0;

    /// Marks message `index` as read, in the field the format keeps it in:
    /// JAM's `TimesRead` and the `times_read` word of a Fido *.msg go to 1,
    /// Squish's `MSGSEEN` bit goes on.
    ///
    /// The one write that changes no part of the message. It patches the field
    /// where it lies rather than going through `replace()`, which would rewrite
    /// the whole record and re-date it: nothing about the message has changed,
    /// only that somebody has now read it.
    ///
    /// A message already marked is left exactly as it is and succeeds — the
    /// count is a mark here and not a tally, so a message read twice is not
    /// written twice. The lock is taken like any other write; a failure means
    /// the mark was not made, which a read-only base is the ordinary reason for.
    [[nodiscard]] virtual tl::expected<void, ErrorPtr> markSeen(uint32_t index) = 0;

protected:
    /// The set a `removeAll()` works from: the numbers sorted, each named once,
    /// and every one of them a message of the base as it now stands.
    ///
    /// Sorted because every format has an order it must take them in — Squish
    /// rewrites the tail of its index from the first of them, JAM walks its own
    /// table alongside — and named once because a number given twice would be
    /// counted twice out of the message count while taking one message out.
    ///
    /// Checked before a byte is written, and the whole set refused where one is
    /// wrong: the caller worked its numbers out before the lock was taken, and a
    /// set that no longer fits the base is a set that no longer means what it
    /// said. The sentence names the number, which is the one thing a caller
    /// could act on.
    [[nodiscard]] static tl::expected<std::vector<uint32_t>, ErrorPtr> sortedTargets(
        const std::vector<uint32_t>& indexes, uint32_t count) {
        std::vector<uint32_t> targets = indexes;
        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
        if (!targets.empty() && (targets.front() == 0 || targets.back() > count)) {
            const uint32_t wrong = targets.front() == 0 ? 0 : targets.back();
            return failure("message " + std::to_string(wrong) +
                           " is not there to delete");
        }
        return targets;
    }
};

}  // namespace amberedit::msgbase
