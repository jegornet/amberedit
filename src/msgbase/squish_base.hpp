#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
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
    [[nodiscard]] tl::expected<void, ErrorPtr> open(const std::string& path, bool echo,
                                                    uint16_t defaultZone) override;
    void close() override;
    [[nodiscard]] tl::expected<void, ErrorPtr> create(const std::string& path) override;

    [[nodiscard]] uint32_t count() const override {
        return static_cast<uint32_t>(index_.size());
    }
    [[nodiscard]] tl::expected<void, ErrorPtr> read(uint32_t index, RawMessage& out,
                                                    bool withText) const override;
    [[nodiscard]] domain::MessageInfo info(uint32_t index) const override;
    [[nodiscard]] uint32_t uidOf(uint32_t index) const override;
    [[nodiscard]] uint32_t indexOfUid(uint32_t uid, bool exact) const override;

    [[nodiscard]] WriteReport writeAll(const std::vector<RawDraft>& drafts) override;
    [[nodiscard]] tl::expected<void, ErrorPtr> replace(uint32_t index,
                                                       const RawDraft& draft) override;
    [[nodiscard]] tl::expected<void, ErrorPtr> removeAll(
        const std::vector<uint32_t>& indexes) override;
    [[nodiscard]] tl::expected<void, ErrorPtr> markSeen(uint32_t index) override;

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

    /// One frame of the free chain as this driver holds it: how much room it
    /// owns, and which frames stand either side of it.
    struct FreeFrame {
        uint32_t prev{0};
        uint32_t next{0};
        uint32_t frameLength{0};
    };

    struct IndexEntry {
        uint32_t offset{0};  ///< where the frame is, 0 for an invalid entry
        uint32_t uid{0};     ///< UMSGID, which the index is sorted by
        uint32_t hash{0};    ///< hash of the To: name, high bit set when read
    };

    [[nodiscard]] tl::expected<void, ErrorPtr> readBaseHeader();
    [[nodiscard]] tl::expected<void, ErrorPtr> writeBaseHeader();
    [[nodiscard]] tl::expected<void, ErrorPtr> loadIndex();
    /// Re-reads the header and the index under the lock, so that a write acts
    /// on the base as it is now rather than as it was when the area opened.
    [[nodiscard]] tl::expected<void, ErrorPtr> reload();

    [[nodiscard]] tl::expected<void, ErrorPtr> readFrame(uint32_t offset,
                                                         Frame& out) const;
    /// The frame header's bytes, laid into `raw` — `kFrameHeaderSize` of them,
    /// whatever room the base keeps for one.
    void encodeFrame(unsigned char* raw, const Frame& frame) const;
    [[nodiscard]] tl::expected<void, ErrorPtr> writeFrame(uint32_t offset,
                                                          const Frame& frame);
    [[nodiscard]] tl::expected<void, ErrorPtr> setFrameNext(uint32_t offset,
                                                            uint32_t value);
    [[nodiscard]] tl::expected<void, ErrorPtr> setFramePrev(uint32_t offset,
                                                            uint32_t value);

    /// Reads more of the free chain into `freeAt_` and `freeBySize_`, stopping
    /// at the first frame that could hold `length` bytes — or at the end of the
    /// chain, which is then known and never walked again.
    ///
    /// **As much of it as is needed and no more.** Reading the whole chain would
    /// cost a message written by hand what a whole set of them costs: an area
    /// with ninety thousand holes in it is ninety thousand reads, and a reader
    /// that spent a tenth of a second on every message somebody sent would have
    /// paid the price of a bulk copy for none of its good. So the walk carries on
    /// from where it left off — `freeNext_` — and a message whose frame is the
    /// first in the chain reads one frame, exactly as it did when the chain was
    /// walked from its head every time.
    ///
    /// Checked as it goes, as the walk always was: each frame free, each
    /// pointing back at the one before it. A chain that does not answer for
    /// itself is forgotten and the failure said.
    [[nodiscard]] tl::expected<void, ErrorPtr> scanFreeChain(uint32_t length);

    /// `scanFreeChain()` asked for a frame no base could hold, which is how the
    /// whole chain is read: nothing satisfies it, so the walk runs to the end.
    ///
    /// **What a set of messages asks for and one message does not.** Reading it
    /// all costs a read per frame, which is too much for a message somebody
    /// typed and nothing at all beside writing thousands — and it is what lets
    /// every message of the set be matched against every hole rather than
    /// against the first one that happens to fit, so an area with a hole for
    /// every message takes the set back into the holes instead of growing.
    static constexpr uint32_t kWholeChain = 0xffffffffu;
    /// Puts the chain back to being a thing on the disk that has not been read.
    void forgetFreeChain();

    /// Takes a frame off the free chain that can hold `length` bytes of
    /// message, or allocates one at the end of the file. `frameLength` comes
    /// back holding what the reused frame owns, which stays as it was.
    ///
    /// **What has been read of the chain is searched in memory, not on the
    /// disk.** It used to be walked frame by frame from its head for every
    /// message, each step a read, and the first frame big enough taken — which
    /// costs nothing while the chain is short and everything once it is not. An
    /// area whose messages have all been deleted has a hole for every one of
    /// them, and the holes too small for the message in hand gather at the head
    /// of the chain as the others are used up, so every message walks past more
    /// of them than the last: ninety thousand messages carried into such an area
    /// is a walk that grows without bound. What has been walked is kept in
    /// `freeBySize_` instead, so no frame is passed over twice.
    ///
    /// The walk itself is still as short as it can be — see `scanFreeChain()`:
    /// what is read is what is needed, and a message that fits the first frame
    /// in the chain costs one read whatever the chain's length.
    ///
    /// And it is the **smallest** frame that fits rather than the first, which
    /// is what the format recommends and what a map ordered by size answers
    /// anyway: the slack left in a frame too big for its message is slack until
    /// a packer comes, so leaving the roomy frames for the messages that need
    /// them wastes less than taking whichever came first.
    [[nodiscard]] tl::expected<void, ErrorPtr> allocateFrame(uint32_t length,
                                                             uint32_t* offset,
                                                             uint32_t* frameLength);
    /// Puts a frame on the free chain, for the next message to grow into.
    [[nodiscard]] tl::expected<void, ErrorPtr> releaseFrame(uint32_t offset, Frame frame);
    /// Notes a frame `releaseFrame()` has just put on the end of the chain, so
    /// that what is held here still answers for what is on the disk. Does
    /// nothing where the chain is not being held; forgets it where it cannot be
    /// kept in step.
    void rememberFreed(uint32_t offset, uint32_t after, uint32_t frameLength);

    /// The message's own bytes as a frame holds them: the XMSG header, then the
    /// control block and its closing NUL, then the text. One block because that
    /// is how it lies on the disk, and a message put down in one write is a
    /// message that cost one write.
    [[nodiscard]] std::string messageBytes(const RawHeader& header, uint32_t uid,
                                           const std::string& control,
                                           const std::string& text) const;

    /// Writes the message itself into the frame at `offset`: the XMSG header,
    /// then the control block, then the text — the order a frame holds them in.
    /// The frame header around it is the caller's. What `replace()` uses, the
    /// frame it writes into being already there.
    [[nodiscard]] tl::expected<void, ErrorPtr> writeMessageAt(uint32_t offset,
                                                              const RawHeader& header,
                                                              uint32_t uid,
                                                              const std::string& control,
                                                              const std::string& text);

    /// The frame and the message in it, in **one** write — what appending uses,
    /// where the frame is new and the two lie next to each other. A message
    /// costs four writes put down piece by piece and one put down like this,
    /// which over a carried set is the difference between a file written in
    /// dribs and a file written.
    [[nodiscard]] tl::expected<void, ErrorPtr> writeFrameWithMessage(
        uint32_t offset, const Frame& frame, const RawHeader& header, uint32_t uid,
        const std::string& control, const std::string& text);

    /// The last `kOriginTailBytes` of the text at `at`, `length` bytes long —
    /// where an echo area's origin line is. What a header-only read looks in
    /// for a sender the XMSG left at zero.
    [[nodiscard]] std::string textTail(uint64_t at, uint32_t length) const;

    /// Puts one draft at the end of the base: a frame for it, the message in
    /// the frame, the frame linked onto the chain, and an index entry pushed
    /// onto the table in memory. `uid` comes back holding the UMSGID it was
    /// given.
    ///
    /// **The lock and the reload are the caller's, and so is the settling.**
    /// Nothing here writes the area header or cuts the index file: those say how
    /// many messages the area holds, and a set written one by one would announce
    /// each of them to every other reader as it went. `writeAll()` does both once
    /// for the whole set — see `settleAfterWriting()`.
    [[nodiscard]] tl::expected<void, ErrorPtr> appendOne(const RawDraft& draft,
                                                         uint32_t* uid);
    /// What a set of appends leaves to be written once it is all in the .sqd:
    /// the index records from `from` to the end in **one** write, the index file
    /// cut to the message count, and the area header last of all.
    ///
    /// The records are left to here rather than written as each message goes in
    /// — a set of ten thousand would otherwise be ten thousand twelve-byte
    /// writes, which is what anybody watching the file sees — and the header
    /// after them, so that nothing outside sees a count the records do not yet
    /// answer for.
    [[nodiscard]] tl::expected<void, ErrorPtr> settleAfterWriting(uint32_t from);

    [[nodiscard]] tl::expected<void, ErrorPtr> writeIndexEntry(uint32_t index);
    /// Writes the index from record `from` to the end of what is now in memory
    /// and cuts the file off there — the one write a delete costs the index,
    /// however many messages it took out. Everything before `from` is where it
    /// was and is not written again.
    [[nodiscard]] tl::expected<void, ErrorPtr> writeIndexTail(uint32_t from);

    BinaryFile data_;
    BinaryFile index_file_;
    BaseHeader base_;
    std::vector<IndexEntry> index_;
    /// The area header states it, and the format says to believe it rather
    /// than a constant: a base whose frame header is not 28 bytes long is not
    /// version one and is left alone.
    uint16_t frameHeaderSize_{28};
    /// The free chain as this driver holds it while it is writing: the frames by
    /// where they are, and the same frames by how much they hold.
    ///
    /// Two views of one thing. `freeBySize_` is what a message is matched
    /// against — `lower_bound` on its length is the smallest frame that fits, or
    /// nothing at all — and `freeAt_` is what says which frames to relink when
    /// one is taken out of the middle.
    ///
    /// Held only as far as this driver's own writing goes: `reload()` forgets
    /// it, since a tosser may have deleted a message meanwhile and the chain on
    /// the disk is the only one that counts.
    std::unordered_map<uint32_t, FreeFrame> freeAt_;
    std::multimap<uint32_t, uint32_t> freeBySize_;
    /// Where the walk left off, and the frame before it — which is what the
    /// next one is checked against, and which moves back when the frame it
    /// names is the one taken out of the chain.
    uint32_t freeNext_{0};
    uint32_t freeScanned_{0};
    bool freeStarted_{false};
    /// Whether the walk has reached the end, in which case what is held here is
    /// every free frame there is and nothing more is on the disk to read.
    bool freeComplete_{false};

    /// Whether this is an echo area, which is the one thing about a Squish
    /// base the driver is told rather than reads: nothing in the files says it,
    /// and it decides whether a message with no address in its header is
    /// answered for out of its origin line.
    bool echo_{false};
};

}  // namespace amberedit::msgbase
