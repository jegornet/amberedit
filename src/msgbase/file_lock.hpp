#pragma once

#include <string>
#include <vector>

#include "support/result.hpp"

namespace amberedit::msgbase {

class BinaryFile;

/// The exclusive lock a message base is written under, held over every file
/// the base is made of and released as one.
///
/// **Nothing writes to a base without it.** A tosser, a scanner and another
/// reader may all have the same area open, and the formats say as much: JAM
/// requires the first byte of the .jhr to be locked before any of its four
/// files is touched, and Squish serialises the header of the .sqd the same way.
/// Half a written frame, an index one record short of its data file, a message
/// counted twice — those are what a base looks like when two writers meet, and
/// none of it is repairable from inside the reader afterwards.
///
/// The lock covers **whole files**, not the single byte the formats name. It
/// still meets them where they lock, byte zero being part of every file, and it
/// says the truthful thing to anything that locks by range: a message is
/// appended at the end of the data file and the index grows with it, so the
/// bytes being written are not the byte being contended for.
///
/// Which files those are is the caller's to name — `.sqd` and `.sqi` for
/// Squish, `.jhr`, `.jdx` and `.jdt` for JAM — and they are taken in the order
/// given. Locks are advisory `fcntl` locks: they hold between processes, and
/// they are dropped by the kernel if this one dies, which is what keeps a
/// crashed reader from leaving an area unwritable.
class FileLock {
public:
    FileLock() = default;
    ~FileLock();

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

    /// Locks every file, or none of them.
    ///
    /// A file already locked by somebody else is waited for and tried again a
    /// few times, everything held so far being let go between attempts: two
    /// writers coming at the same base from opposite ends of the list would
    /// otherwise hold one file each and wait for the other for ever. A failure
    /// means the base is busy and names the file, and the caller must not write.
    [[nodiscard]] Result<void> acquire(const std::vector<BinaryFile*>& files);

    void release();

    [[nodiscard]] bool held() const { return !locked_.empty(); }

private:
    std::vector<int> locked_;  ///< descriptors currently holding a lock
};

}  // namespace amberedit::msgbase
