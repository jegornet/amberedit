#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "msgbase/binary_file.hpp"
#include "msgbase/format_driver.hpp"

namespace amberedit::msgbase {

/// The JAM message base (JAM-001), read and written directly.
///
/// Three files: `<area>.jhr` starts with a 1024-byte info block and holds the
/// message headers, each a 76-byte fixed part followed by "subfields" — name,
/// subject, addresses and kludges, each a typed length-prefixed record;
/// `<area>.jdt` holds the text; `<area>.jdx` is the index, two dwords per
/// message number. Nothing is ever moved: a deleted message keeps its header,
/// marked deleted, and its index record is blanked, so "message 5" in the
/// index need not be the fifth active message. The driver therefore keeps a
/// table of the active messages, built from the index when the base is opened
/// and rebuilt whenever it is locked for writing.
///
/// A message's UID is its JAM message number — the index record's position
/// plus BaseMsgNum — which survives a pack, since packing renumbers by moving
/// BaseMsgNum rather than the records.
class JamBase final : public FormatDriver {
public:
    [[nodiscard]] Result<void> open(const std::string& path, bool echo,
                                    uint16_t defaultZone) override;
    void close() override;
    [[nodiscard]] Result<void> create(const std::string& path) override;

    [[nodiscard]] uint32_t count() const override {
        return static_cast<uint32_t>(active_.size());
    }
    [[nodiscard]] Result<void> read(uint32_t index, RawMessage& out,
                                    bool withText) const override;
    [[nodiscard]] domain::MessageInfo info(uint32_t index) const override;
    [[nodiscard]] uint32_t uidOf(uint32_t index) const override;
    [[nodiscard]] uint32_t indexOfUid(uint32_t uid, bool exact) const override;

    [[nodiscard]] Result<uint32_t> write(const RawDraft& draft) override;
    [[nodiscard]] Result<void> replace(uint32_t index, const RawDraft& draft) override;
    [[nodiscard]] Result<void> remove(uint32_t index) override;
    [[nodiscard]] Result<void> markSeen(uint32_t index) override;

private:
    /// The info block at offset 0 of the .jhr, in the fields we act on.
    struct Info {
        uint32_t dateCreated{0};
        uint32_t modCounter{0};
        uint32_t activeMessages{0};
        uint32_t passwordCrc{0xffffffffu};
        uint32_t baseMessageNumber{1};
    };

    /// The fixed part of one message header.
    struct Header {
        uint32_t subfieldLength{0};
        uint32_t timesRead{0};
        uint32_t msgIdCrc{0xffffffffu};
        uint32_t replyCrc{0xffffffffu};
        uint32_t replyTo{0};
        uint32_t replyFirst{0};
        uint32_t replyNext{0};
        uint32_t dateWritten{0};
        uint32_t dateReceived{0};
        uint32_t dateProcessed{0};
        uint32_t number{0};
        uint32_t attributes{0};
        uint32_t attributes2{0};
        uint32_t textOffset{0};
        uint32_t textLength{0};
        uint32_t passwordCrc{0xffffffffu};
        uint32_t cost{0};
    };

    /// One active message: where its records are, and the fixed header so
    /// that a message list costs no disk at all.
    struct ActiveMessage {
        uint32_t indexRecord{0};  ///< 0-based record number in the .jdx
        uint32_t headerOffset{0};
        Header header;
    };

    struct Subfield {
        uint16_t id{0};
        std::string data;
    };

    [[nodiscard]] Result<void> readInfo();
    [[nodiscard]] Result<void> writeInfo();
    /// Builds the table of active messages from the index and the headers.
    [[nodiscard]] Result<void> loadActive();
    [[nodiscard]] Result<void> reload();

    [[nodiscard]] Result<void> readHeaderAt(uint32_t offset, Header& out) const;
    [[nodiscard]] Result<void> readSubfields(const ActiveMessage& message,
                                             std::vector<Subfield>& out) const;

    /// Turns a draft into the two blocks JAM stores it as: the subfields —
    /// names, subject, addresses and the kludges the format keeps as data — and
    /// the text with the routing lifted out of it. `header` comes back with the
    /// subfield length and the two CRCs the kludges decide; where the blocks go
    /// is the caller's, and is the whole difference between writing a message
    /// and changing one.
    void encodeDraft(const RawDraft& draft, Header& header, std::string& subfieldBlock,
                     std::string& text) const;
    /// Writes the fixed header and the subfields at `offset`.
    [[nodiscard]] Result<void> writeHeaderAt(uint32_t offset, const Header& header,
                                             const std::string& subfields);
    /// Writes the index record of message number `record`: the CRC of the name
    /// the message is addressed to, and where its header is.
    [[nodiscard]] Result<void> writeIndexRecord(uint32_t record, const std::string& to,
                                                uint32_t headerOffset);

    /// The UID of an active-table entry: its index record plus BaseMsgNum.
    [[nodiscard]] uint32_t uidOfEntry(const ActiveMessage& message) const;

    BinaryFile headers_;
    BinaryFile index_;
    BinaryFile text_;
    Info info_;
    std::vector<ActiveMessage> active_;
    bool echo_{false};
};

}  // namespace amberedit::msgbase
