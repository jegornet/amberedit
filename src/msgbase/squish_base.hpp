#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "msgbase/binary_file.hpp"
#include "msgbase/format_driver.hpp"

namespace amberedit::msgbase {

/// The Squish message base (FSP-1037), read and written directly.
///
/// Two files: `<area>.sqd` holds an area header and then the messages, each in
/// a "frame" of a doubly linked list, and `<area>.sqi` an array of fixed-width
/// index records — one per message, in message order, each naming the frame and
/// carrying the message's UMSGID. The index is what makes "message 27" a
/// question that can be answered without walking the chain; the chain is what
/// makes a message deletable without rewriting the file.
///
/// The index is read into memory when the area is opened and re-read whenever
/// the base is locked for writing. That is not a cache to be kept in step with
/// the disk: a write takes the lock, reads both the header and the index again,
/// and works from what it finds, because a tosser may have appended a dozen
/// messages since the area was opened.
class SquishBase final : public FormatDriver {
public:
    bool open(const std::string& path, bool echo, uint16_t defaultZone) override;
    void close() override;
    bool create(const std::string& path) override;

    [[nodiscard]] uint32_t count() const override {
        return static_cast<uint32_t>(index_.size());
    }
    [[nodiscard]] bool read(uint32_t index, RawMessage& out,
                            bool withText) const override;
    [[nodiscard]] domain::MessageInfo info(uint32_t index) const override;
    [[nodiscard]] uint32_t uidOf(uint32_t index) const override;
    [[nodiscard]] uint32_t indexOfUid(uint32_t uid, bool exact) const override;

    uint32_t write(const RawDraft& draft) override;
    bool replace(uint32_t index, const RawDraft& draft) override;
    bool remove(uint32_t index) override;
    bool markSeen(uint32_t index) override;

private:
    /// The area header at offset 0 of the .sqd, in the fields we act on. What
    /// is not here is not ours to change and is written back as it was read.
    struct BaseHeader {
        uint32_t messageCount{0};
        uint32_t highMessage{0};
        uint32_t skipMessages{0};
        uint32_t highWater{0};
        uint32_t nextUid{1};
        uint32_t firstFrame{0};
        uint32_t lastFrame{0};
        uint32_t firstFree{0};
        uint32_t lastFree{0};
        uint32_t endFrame{0};
        uint32_t maxMessages{0};
        uint16_t keepDays{0};
    };

    /// The frame header before every message and before every free block.
    struct Frame {
        uint32_t next{0};
        uint32_t prev{0};
        uint32_t frameLength{0};    ///< space the frame owns, header excluded
        uint32_t messageLength{0};  ///< space it uses: XMSG, control and text
        uint32_t controlLength{0};
        uint16_t type{0};
    };

    struct IndexEntry {
        uint32_t offset{0};  ///< where the frame is, 0 for an invalid entry
        uint32_t uid{0};     ///< UMSGID, which the index is sorted by
        uint32_t hash{0};    ///< hash of the To: name, high bit set when read
    };

    [[nodiscard]] bool readBaseHeader();
    [[nodiscard]] bool writeBaseHeader();
    [[nodiscard]] bool loadIndex();
    /// Re-reads the header and the index under the lock, so that a write acts
    /// on the base as it is now rather than as it was when the area opened.
    [[nodiscard]] bool reload();

    [[nodiscard]] bool readFrame(uint32_t offset, Frame& out) const;
    [[nodiscard]] bool writeFrame(uint32_t offset, const Frame& frame);
    [[nodiscard]] bool setFrameNext(uint32_t offset, uint32_t value);
    [[nodiscard]] bool setFramePrev(uint32_t offset, uint32_t value);

    /// Takes a frame off the free chain that can hold `length` bytes of
    /// message, or allocates one at the end of the file. `frameLength` comes
    /// back holding what the reused frame owns, which stays as it was.
    [[nodiscard]] bool allocateFrame(uint32_t length, uint32_t* offset,
                                     uint32_t* frameLength);
    /// Puts a frame on the free chain, for the next message to grow into.
    [[nodiscard]] bool releaseFrame(uint32_t offset, Frame frame);

    /// Writes the message itself into the frame at `offset`: the XMSG header,
    /// then the control block, then the text — the order a frame holds them in.
    /// The frame header around it is the caller's.
    [[nodiscard]] bool writeMessageAt(uint32_t offset, const RawHeader& header,
                                      uint32_t uid, const std::string& control,
                                      const std::string& text);

    [[nodiscard]] bool writeIndexEntry(uint32_t index);

    BinaryFile data_;
    BinaryFile index_file_;
    BaseHeader base_;
    std::vector<IndexEntry> index_;
    /// The area header states it, and the format says to believe it rather
    /// than a constant: a base whose frame header is not 28 bytes long is not
    /// version one and is left alone.
    uint16_t frameHeaderSize_{28};
};

}  // namespace amberedit::msgbase
