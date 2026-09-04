#pragma once

#include <cstddef>
#include <cstdint>

namespace amberedit::sys {

/// Positional file access and whole-file locking, on a descriptor.
///
/// A descriptor and not a handle of our own: everything above this opens files
/// with `open()`, which mingw provides on Windows as readily as POSIX does, and
/// only these four operations have no Windows spelling. Changing the whole of
/// `BinaryFile` to carry a `HANDLE` would buy nothing that these four do not.

/// Reads at `offset` without disturbing wherever the descriptor was pointing.
/// Answers the number of bytes moved, 0 at end of file, or -1 with `errno` set.
[[nodiscard]] int64_t readAt(int fd, void* out, size_t count, uint64_t offset);

/// Writes at `offset`, under the same contract.
[[nodiscard]] int64_t writeAt(int fd, const void* data, size_t count, uint64_t offset);

/// Takes an exclusive lock on the whole file, including whatever it grows into,
/// and does not wait: a file another process holds is refused at once so that
/// the caller can decide how long to keep trying.
///
/// The two platforms differ in a way worth knowing rather than papering over.
/// POSIX `fcntl` locks are advisory — a tosser that takes no lock writes anyway
/// — while Windows locks are mandatory, and a process holding one makes another
/// process's plain read fail. AmberEdit holds a lock only across a single write,
/// which is short enough that the difference does not turn into a tosser
/// failing to deliver mail.
[[nodiscard]] bool lockWholeFile(int fd);

/// Gives the lock back. Answers false only if the descriptor was not held.
bool unlockWholeFile(int fd);

}  // namespace amberedit::sys
