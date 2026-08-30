#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "encoding/charset_detector.hpp"
#include "encoding/iconv_recoder.hpp"
#include "msgbase/format_driver.hpp"
#include "ports/i_msgbase.hpp"

namespace amberedit::msgbase {

/// IMsgBase implemented on AmberEdit's own format drivers. One adapter serves
/// every format — Squish, JAM and Fido *.msg (FTS-0001): the base type arrives
/// in AreaConfig::type, or is worked out from what is on disk, and picks the
/// driver on open.
///
/// The drivers speak bytes; this class is where a stored message becomes the
/// domain's. Character sets are converted here and only here — the CHRS kludge
/// is read out of the message itself, and everything above this adapter is
/// UTF-8. Locking is the drivers' own: every write and delete takes the base's
/// files for the duration and no longer.
class FtnMsgBase final : public ports::IMsgBase {
public:
    /// @param defaultCharset charset a message being read is decoded from when
    ///        it has no CHRS kludge, or one naming nothing particular
    ///        ("IBMPC"). Reading only — a message is written in the charset its
    ///        own draft names, which compose_charset decides.
    /// @param fieldLimits whether the From, To and Subject of a message being
    ///        written are cut to the room FTS-0001 keeps for them, from
    ///        `compose_fts1_field_limits`. See encode(), which is where the cut is
    ///        made and where the bytes it counts are known.
    explicit FtnMsgBase(std::string_view defaultCharset = "CP866",
                        bool fieldLimits = true);
    ~FtnMsgBase() override;

    FtnMsgBase(const FtnMsgBase&) = delete;
    FtnMsgBase& operator=(const FtnMsgBase&) = delete;

    [[nodiscard]] tl::expected<void, ErrorPtr> open(
        const domain::AreaConfig& area) override;
    void close() override;

    /// Creates the base an area names, empty, and leaves it closed.
    ///
    /// An area the tosser config declares has no base on disk until something
    /// writes into it, and a reader that refused to enter such an area would
    /// refuse exactly the one that a first message is wanted in. This is what
    /// entering it makes instead: the files of the format the config states,
    /// holding no messages.
    ///
    /// It refuses unless isAbsent() holds, so nothing that is already on disk
    /// is written over, and refuses an area whose type nothing states — there
    /// is no base to probe and no format to guess at. A failure says why and
    /// leaves the disk as it was.
    [[nodiscard]] tl::expected<void, ErrorPtr> create(const domain::AreaConfig& area);

    /// Whether the area has a type and nothing at all stands at its path.
    ///
    /// This is the one state creating a base answers: a base that is half
    /// there, or there and unreadable, holds something, and an empty one
    /// written over it would take that something with it.
    [[nodiscard]] static bool isAbsent(const domain::AreaConfig& area);

    [[nodiscard]] uint32_t count() const override;
    [[nodiscard]] domain::MessageHeader header(uint32_t index) const override;
    [[nodiscard]] domain::MessageBody body(uint32_t index) const override;

    [[nodiscard]] domain::MessageThread thread(uint32_t index) const override;

    [[nodiscard]] domain::MessageInfo info(uint32_t index) const override;

    [[nodiscard]] uint32_t uidOf(uint32_t index) const override;
    [[nodiscard]] uint32_t indexOfUid(uint32_t uid) const override;

    [[nodiscard]] tl::expected<uint32_t, ErrorPtr> write(
        const domain::MessageDraft& draft) override;
    [[nodiscard]] tl::expected<void, ErrorPtr> replace(
        uint32_t index, const domain::MessageDraft& draft) override;
    [[nodiscard]] tl::expected<void, ErrorPtr> remove(uint32_t index) override;
    [[nodiscard]] tl::expected<void, ErrorPtr> markSeen(uint32_t index) override;

    [[nodiscard]] bool isOpen() const { return driver_ != nullptr; }

    /// Works out the base type from what is on disk: <path>.sqd means Squish,
    /// <path>.jhr means JAM, a directory means Fido *.msg. Needed when the
    /// tosser config states no type (no -b option). Unknown means nothing fit.
    static domain::MsgBaseType probeType(const std::string& path);

private:
    /// The draft as the drivers take it: every string encoded out of UTF-8 into
    /// the charset the draft names, and the text made into the hard carriage
    /// returns FTS-0001 asks for. The stamps are left empty — writing a message
    /// dates it here, changing one keeps the date the base already holds.
    ///
    /// It is also where the header fields are held to the byte lengths FTS-0001
    /// gives them, `compose_fts1_field_limits` asking. That is a cut in the charset
    /// the message is written in, so this is the only place it can be made: a
    /// name of 35 Cyrillic letters is 35 bytes in CP866 and 70 in UTF-8, and
    /// above this class the message is UTF-8 and nothing else.
    [[nodiscard]] RawDraft encode(const domain::MessageDraft& draft) const;

    std::unique_ptr<FormatDriver> driver_;
    domain::AreaConfig areaConfig_;
    encoding::CharsetDetector detector_;
    bool fieldLimits_{true};
    mutable encoding::IconvRecoder recoder_;
};

}  // namespace amberedit::msgbase
