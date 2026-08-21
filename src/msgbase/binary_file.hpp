#pragma once

#include <cstdint>
#include <string>

namespace amberedit::msgbase {

/// One file of a message base, held open for as long as the base is.
///
/// A descriptor rather than a `FILE*`, for two reasons that decide the whole
/// shape of this class: the locks the formats require are `fcntl` locks, which
/// are taken on a descriptor, and every read and write here is at a stated
/// offset, which `pread`/`pwrite` do without a seek of their own. Nothing in a
/// message base is read sequentially — a frame, a header, an index record are
/// all "so many bytes at this offset" — so there is no buffering to be had and
/// no file position worth keeping.
class BinaryFile {
public:
    BinaryFile() = default;
    ~BinaryFile();

    BinaryFile(const BinaryFile&) = delete;
    BinaryFile& operator=(const BinaryFile&) = delete;

    /// Opens an existing file. Read-write where the caller asks for it and the
    /// file system allows it; `writable()` says which it turned out to be, so
    /// that a base on a read-only medium can still be read.
    bool open(const std::string& path, bool wantWritable);

    /// Creates the file, failing if it is already there. The exclusive create
    /// is what makes two writers picking the same Fido *.msg number harmless.
    bool create(const std::string& path);

    void close();

    [[nodiscard]] bool isOpen() const { return fd_ >= 0; }
    [[nodiscard]] bool writable() const { return writable_; }
    /// For the file lock, which is taken on the descriptor. No other caller has
    /// any business with it.
    [[nodiscard]] int descriptor() const { return fd_; }
    [[nodiscard]] const std::string& path() const { return path_; }

    /// Reads exactly `count` bytes. False means the file is shorter than that
    /// or the read failed; a short read is never silently accepted, since every
    /// record here is fixed width and half of one says nothing.
    [[nodiscard]] bool readAt(uint64_t offset, void* out, size_t count) const;
    [[nodiscard]] bool writeAt(uint64_t offset, const void* data, size_t count);

    /// Size in bytes, or -1 when it cannot be determined.
    [[nodiscard]] int64_t size() const;
    [[nodiscard]] bool truncate(uint64_t size);

private:
    int fd_{-1};
    bool writable_{false};
    std::string path_;
};

}  // namespace amberedit::msgbase
