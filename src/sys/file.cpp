#include "sys/file.hpp"

#include <cerrno>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace amberedit::sys {

#ifdef _WIN32

namespace {

/// The Windows handle behind a descriptor, or INVALID_HANDLE_VALUE with `errno`
/// already set for a descriptor that is not open.
HANDLE handleOf(int fd) {
    const auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
    if (handle == INVALID_HANDLE_VALUE) errno = EBADF;
    return handle;
}

/// An OVERLAPPED carrying nothing but the offset. On a synchronous handle this
/// is what makes ReadFile and WriteFile positional — which is the whole point,
/// since seeking first and reading after would move a file pointer that the
/// caller believes it still owns.
OVERLAPPED at(uint64_t offset) {
    OVERLAPPED where{};
    where.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFu);
    where.OffsetHigh = static_cast<DWORD>(offset >> 32);
    return where;
}

/// Reports a Windows failure through `errno`, which is what the callers read.
/// Only the distinctions AmberEdit acts on are drawn; everything else is EIO.
int64_t failed() {
    switch (::GetLastError()) {
        case ERROR_HANDLE_EOF: return 0;
        case ERROR_ACCESS_DENIED: errno = EACCES; break;
        case ERROR_LOCK_VIOLATION: errno = EACCES; break;
        case ERROR_INVALID_HANDLE: errno = EBADF; break;
        case ERROR_DISK_FULL: errno = ENOSPC; break;
        default: errno = EIO; break;
    }
    return -1;
}

/// A single lock request over the whole file. Windows takes a length rather
/// than POSIX's "zero means to the end, wherever that goes", so the largest
/// length there is stands in — it covers whatever the file grows into.
constexpr DWORD kWholeFileLow = 0xFFFFFFFFu;
constexpr DWORD kWholeFileHigh = 0xFFFFFFFFu;

}  // namespace

int64_t readAt(int fd, void* out, size_t count, uint64_t offset) {
    const HANDLE handle = handleOf(fd);
    if (handle == INVALID_HANDLE_VALUE) return -1;

    OVERLAPPED where = at(offset);
    DWORD got = 0;
    if (::ReadFile(handle, out, static_cast<DWORD>(count), &got, &where) == 0) return failed();
    return static_cast<int64_t>(got);
}

int64_t writeAt(int fd, const void* data, size_t count, uint64_t offset) {
    const HANDLE handle = handleOf(fd);
    if (handle == INVALID_HANDLE_VALUE) return -1;

    OVERLAPPED where = at(offset);
    DWORD put = 0;
    if (::WriteFile(handle, data, static_cast<DWORD>(count), &put, &where) == 0) return failed();
    return static_cast<int64_t>(put);
}

bool lockWholeFile(int fd) {
    const HANDLE handle = handleOf(fd);
    if (handle == INVALID_HANDLE_VALUE) return false;

    OVERLAPPED where = at(0);
    return ::LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                        kWholeFileLow, kWholeFileHigh, &where) != 0;
}

bool unlockWholeFile(int fd) {
    const HANDLE handle = handleOf(fd);
    if (handle == INVALID_HANDLE_VALUE) return false;

    OVERLAPPED where = at(0);
    return ::UnlockFileEx(handle, 0, kWholeFileLow, kWholeFileHigh, &where) != 0;
}

#else

int64_t readAt(int fd, void* out, size_t count, uint64_t offset) {
    return ::pread(fd, out, count, static_cast<off_t>(offset));
}

int64_t writeAt(int fd, const void* data, size_t count, uint64_t offset) {
    return ::pwrite(fd, data, count, static_cast<off_t>(offset));
}

namespace {

/// `l_len` of zero means "to the end of the file, however far that moves".
bool setWholeFileLock(int fd, short type) {
    struct flock request{};
    request.l_type = type;
    request.l_whence = SEEK_SET;
    request.l_start = 0;
    request.l_len = 0;
    while (::fcntl(fd, F_SETLK, &request) == -1) {
        if (errno != EINTR) return false;
    }
    return true;
}

}  // namespace

bool lockWholeFile(int fd) {
    return setWholeFileLock(fd, F_WRLCK);
}

bool unlockWholeFile(int fd) {
    return setWholeFileLock(fd, F_UNLCK);
}

#endif

}  // namespace amberedit::sys
