#include "msgbase/binary_file.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>

namespace amberedit::msgbase {

namespace {

/// The mode a base file is created with: readable and writable by the user
/// and the group, as the husky tools create echomail bases. A tosser and a
/// reader run as different users often enough that a group-writable base is
/// the normal case, and the umask still has the last word.
constexpr mode_t kFileMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;

}  // namespace

BinaryFile::~BinaryFile() {
    close();
}

bool BinaryFile::open(const std::string& path, bool wantWritable) {
    close();
    path_ = path;

    if (wantWritable) {
        fd_ = ::open(path.c_str(), O_RDWR);
        if (fd_ >= 0) {
            writable_ = true;
            return true;
        }
        // A base whose files are not ours to write is still ours to read, and
        // that is worth more than an error: a reader on someone else's spool
        // opens the area and refuses only the writing.
        if (errno != EACCES && errno != EROFS && errno != EPERM) return false;
    }

    fd_ = ::open(path.c_str(), O_RDONLY);
    writable_ = false;
    return fd_ >= 0;
}

bool BinaryFile::create(const std::string& path) {
    close();
    path_ = path;
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, kFileMode);
    writable_ = fd_ >= 0;
    return fd_ >= 0;
}

void BinaryFile::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    writable_ = false;
    path_.clear();
}

bool BinaryFile::readAt(uint64_t offset, void* out, size_t count) const {
    if (fd_ < 0) return false;
    auto* cursor = static_cast<unsigned char*>(out);
    size_t done = 0;
    while (done < count) {
        const ssize_t got =
            ::pread(fd_, cursor + done, count - done, static_cast<off_t>(offset + done));
        if (got < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (got == 0) return false;  // end of file: the record is not all there
        done += static_cast<size_t>(got);
    }
    return true;
}

bool BinaryFile::writeAt(uint64_t offset, const void* data, size_t count) {
    if (fd_ < 0 || !writable_) return false;
    const auto* cursor = static_cast<const unsigned char*>(data);
    size_t done = 0;
    while (done < count) {
        const ssize_t put =
            ::pwrite(fd_, cursor + done, count - done, static_cast<off_t>(offset + done));
        if (put <= 0) {
            if (put < 0 && errno == EINTR) continue;
            return false;
        }
        done += static_cast<size_t>(put);
    }
    return true;
}

int64_t BinaryFile::size() const {
    if (fd_ < 0) return -1;
    struct stat info{};
    if (::fstat(fd_, &info) != 0) return -1;
    return static_cast<int64_t>(info.st_size);
}

bool BinaryFile::truncate(uint64_t size) {
    if (fd_ < 0 || !writable_) return false;
    return ::ftruncate(fd_, static_cast<off_t>(size)) == 0;
}

}  // namespace amberedit::msgbase
