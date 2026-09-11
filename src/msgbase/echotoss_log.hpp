#pragma once

#include <string>

namespace amberedit::msgbase {

/// Adds an area's name to the file `echotosslog` names, one name per line.
///
/// It is how a tosser is told an area has something new in it. AmberEdit writes
/// a message into a base and nothing else on the system knows it is there; the
/// name of the area goes into this file instead, and the tosser pointed at it
/// scans those areas out on its next run.
///
/// An empty `path` — what a config naming no `echotosslog` leaves — writes
/// nowhere, and so does an area with an empty tag: a line here is an area's
/// name, and an area that has none cannot be named.
///
/// Nothing is read back before the name is added. One area written into twice
/// is two lines, which is what a tosser that takes the file away expects: what
/// this file holds is what has happened since it was last emptied, and dropping
/// a repeat would make it hold the state of somebody else's run instead.
///
/// A failure to write is passed over in silence. The message is in the base by
/// the time this is called, and a log that would not open must not turn a
/// message that was written into one the editor says was not — there is no
/// second attempt to be had from the user, only a second copy of the message.
void appendEchotossLog(const std::string& path, const std::string& tag);

}  // namespace amberedit::msgbase
