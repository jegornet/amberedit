#include "msgbase/file_lock.hpp"

#include <cerrno>
#include <chrono>
#include <thread>

#include "msgbase/binary_file.hpp"
#include "sys/file.hpp"

namespace amberedit::msgbase {

namespace {

/// How long a busy base is waited for: ten attempts a fifth of a second apart.
/// A tosser holds a base for the length of one message, so two seconds is a
/// generous wait, and waiting for ever is not on offer — the reader has a
/// screen to keep answering.
constexpr int kAttempts = 10;
constexpr std::chrono::milliseconds kRetryPause{200};

void waitABit() {
    std::this_thread::sleep_for(kRetryPause);
}

}  // namespace

FileLock::~FileLock() {
    release();
}

tl::expected<void, ErrorPtr> FileLock::acquire(const std::vector<BinaryFile*>& files) {
    release();

    std::string reason;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        if (attempt != 0) waitABit();

        bool gotAll = true;
        for (BinaryFile* file : files) {
            if (file == nullptr || !file->isOpen()) continue;
            if (!sys::lockWholeFile(file->descriptor())) {
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
        (void)sys::unlockWholeFile(*fd);
    }
    locked_.clear();
}

}  // namespace amberedit::msgbase
