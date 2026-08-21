#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "msgbase/format_driver.hpp"

namespace amberedit::msgbase {

/// The Fido *.msg base (FTS-0001), one message per file, read and written
/// directly.
///
/// The base is a directory; a message is `<N>.msg` in it, a 190-byte header
/// and then the text, kludges inline at its head and a NUL closing it. `N` is
/// the message's identity — the UID — and the position is nothing but where
/// the number lands when the directory listing is sorted. There is no index
/// and no counter: the directory is scanned when the area is opened, and
/// again under the lock before every write.
///
/// Concurrent writers are kept apart by the files themselves: a new message
/// is created with O_EXCL, so two editors picking the same number cannot both
/// have it — the loser rescans and takes the next.
class SdmBase final : public FormatDriver {
public:
    [[nodiscard]] Result<void> open(const std::string& path, bool echo,
                                    uint16_t defaultZone) override;
    void close() override;
    [[nodiscard]] Result<void> create(const std::string& path) override;

    [[nodiscard]] uint32_t count() const override {
        return static_cast<uint32_t>(numbers_.size());
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
    [[nodiscard]] Result<void> scan();
    [[nodiscard]] std::string fileFor(uint32_t number) const;

    /// The message as the file holds it: the 190-byte header, and the body —
    /// the kludges inline at its head, the text after them and the NUL FTS-0001
    /// closes it with. Shared by writing a message and changing one, which
    /// differ only in which file the two blocks go to.
    void encodeHeader(const RawHeader& header, unsigned char* raw) const;
    [[nodiscard]] std::string encodeBody(const RawDraft& draft) const;

    std::string directory_;
    /// The message numbers on disk, sorted. Position i+1 reads numbers_[i].
    std::vector<uint32_t> numbers_;
    bool echo_{false};
    uint16_t defaultZone_{2};
};

}  // namespace amberedit::msgbase
