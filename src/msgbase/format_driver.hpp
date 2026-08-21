#pragma once

#include <cstdint>
#include <string>

#include "msgbase/raw_message.hpp"

namespace amberedit::msgbase {

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
    virtual bool open(const std::string& path, bool echo, uint16_t defaultZone) = 0;
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
    /// over. false says why in lastError().
    virtual bool create(const std::string& path) = 0;

    [[nodiscard]] virtual uint32_t count() const = 0;

    /// Reads message `index`. `withText` false stops at the header and the
    /// control lines, which is all a message list needs and, in every format,
    /// a good deal less to read.
    [[nodiscard]] virtual bool read(uint32_t index, RawMessage& out,
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

    /// Appends a message and hands back its number, or 0 on failure.
    ///
    /// **Every write takes the base's lock first and gives it back after**, the
    /// whole of it under `FileLock`: a tosser may be writing the same area
    /// between two keystrokes.
    virtual uint32_t write(const RawDraft& draft) = 0;

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
    /// Takes the lock like every other write. false leaves the message as it
    /// was; see lastError() for why.
    virtual bool replace(uint32_t index, const RawDraft& draft) = 0;

    /// Takes a message out. Everything after it moves up one.
    virtual bool remove(uint32_t index) = 0;

    /// Marks message `index` as read, in the field the format keeps it in:
    /// JAM's `TimesRead` and the `times_read` word of a Fido *.msg go to 1,
    /// Squish's `MSGSEEN` bit goes on.
    ///
    /// The one write that changes no part of the message. It patches the field
    /// where it lies rather than going through `replace()`, which would rewrite
    /// the whole record and re-date it: nothing about the message has changed,
    /// only that somebody has now read it.
    ///
    /// A message already marked is left exactly as it is and true comes back —
    /// the count is a mark here and not a tally, so a message read twice is not
    /// written twice. The lock is taken like any other write; false means the
    /// mark was not made, which a read-only base is the ordinary reason for.
    virtual bool markSeen(uint32_t index) = 0;

    [[nodiscard]] const std::string& lastError() const { return lastError_; }

protected:
    void setError(std::string message) const { lastError_ = std::move(message); }
    void clearError() const { lastError_.clear(); }

private:
    /// Mutable so that a failed read — a `const` operation as far as the caller
    /// is concerned — can still say why it failed.
    mutable std::string lastError_;
};

}  // namespace amberedit::msgbase
