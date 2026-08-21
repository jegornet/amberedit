#include "msgbase/file_lock.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <ctime>

#include "msgbase/binary_file.hpp"

namespace amberedit::msgbase {

namespace {

/// How long a busy base is waited for: ten attempts a fifth of a second apart.
/// A tosser holds a base for the length of one message, so two seconds is a
/// generous wait, and waiting for ever is not on offer — the reader has a
/// screen to keep answering.
constexpr int kAttempts = 10;
constexpr long kRetryNanoseconds = 200L * 1000L * 1000L;

/// Whole-file exclusive lock: `l_len` of zero means "to the end of the file,
/// however far that moves".
bool lockWholeFile(int fd, bool exclusive) {
    struct flock request{};
    request.l_type = exclusive ? F_WRLCK : F_UNLCK;
    request.l_whence = SEEK_SET;
    request.l_start = 0;
    request.l_len = 0;
    while (::fcntl(fd, F_SETLK, &request) == -1) {
        if (errno != EINTR) return false;
    }
    return true;
}

void waitABit() {
    struct timespec pause{};
    pause.tv_sec = 0;
    pause.tv_nsec = kRetryNanoseconds;
    while (::nanosleep(&pause, &pause) == -1 && errno == EINTR) {
    }
}

}  // namespace

FileLock::~FileLock() {
    release();
}

Result<void> FileLock::acquire(const std::vector<BinaryFile*>& files) {
    release();

    std::string reason;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        if (attempt != 0) waitABit();

        bool gotAll = true;
        for (BinaryFile* file : files) {
            if (file == nullptr || !file->isOpen()) continue;
            if (!lockWholeFile(file->descriptor(), true)) {
                reason = "cannot lock " + file->path();
                gotAll = false;
                break;
            }
            locked_.push_back(file->descriptor());
        }
        if (gotAll) return {};
        release();
    }
    return failure(std::move(reason));
}

void FileLock::release() {
    // Backwards, so that the file another writer is most likely waiting on —
    // the first of the list, which is the one the formats name — is the last to
    // come free and cannot be taken while we still hold the rest.
    for (auto fd = locked_.rbegin(); fd != locked_.rend(); ++fd) {
        (void)lockWholeFile(*fd, false);
    }
    locked_.clear();
}

}  // namespace amberedit::msgbase
