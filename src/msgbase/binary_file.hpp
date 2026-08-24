#pragma once

#include <cstdint>
#include <string>

namespace amberedit::msgbase {

/// Whether the bytes moved, and why they did not.
///
/// `readAt` and `writeAt` used to answer a plain `bool`, and one false covered
/// two different things: a file shorter than the record asked for, and a read
/// the kernel refused. The second came with an `errno` that was dropped on the
/// floor, so a base on a failing disk and a base truncated by a half-finished
/// tosser said exactly the same nothing.
///
/// **Deliberately not convertible to `bool`.** Swapping the return type for one
/// that was would have flipped the sense of every `if (!file.readAt(…))` in the
/// three drivers without a word from the compiler. `ok()` and `failed()` have to
/// be written out, and the compiler names every site that has not been.
class IoStatus {
public:
    /// The bytes moved.
    [[nodiscard]] static IoStatus moved() { return IoStatus{0, false}; }
    /// The file holds fewer bytes than the record wanted. Not an `errno`
    /// condition: `pread` says so by returning zero.
    [[nodiscard]] static IoStatus truncated() { return IoStatus{0, true}; }
    /// The kernel refused, with the `errno` it left behind.
    [[nodiscard]] static IoStatus refused(int errnum) { return IoStatus{errnum, false}; }

    [[nodiscard]] bool ok() const { return errnum_ == 0 && !truncated_; }
    [[nodiscard]] bool failed() const { return !ok(); }

    /// Why, for a driver to put its path and offset in front of. Empty where it
    /// worked.
    [[nodiscard]] std::string message() const;

private:
    IoStatus(int errnum, bool truncated) : errnum_(errnum), truncated_(truncated) {}

    int errnum_{0};
    bool truncated_{false};
};

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

    /// Reads exactly `count` bytes. A short read is never silently accepted,
    /// since every record here is fixed width and half of one says nothing —
    /// and the status tells a short file from a refused read.
    [[nodiscard]] IoStatus readAt(uint64_t offset, void* out, size_t count) const;
    [[nodiscard]] IoStatus writeAt(uint64_t offset, const void* data, size_t count);

    /// Size in bytes, or -1 when it cannot be determined.
    [[nodiscard]] int64_t size() const;
    [[nodiscard]] bool truncate(uint64_t size);

private:
    int fd_{-1};
    bool writable_{false};
    std::string path_;
};

}  // namespace amberedit::msgbase
