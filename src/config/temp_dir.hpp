#pragma once

#include <string>

#include "support/error.hpp"

namespace amberedit::config {

/// The directory temporary work goes in, made if it was not there and handed
/// back ready to be written to. `configured` is `tmpdir` from the config, and
/// empty where it names none.
///
/// Empty is the ordinary case and not a failure: the system has a temporary
/// directory of its own — `$TMPDIR` and then `/tmp` — and an `amberedit`
/// directory of the user's own inside it is what temporary work falls back on.
/// `tmpdir` is then for naming somewhere else: a filesystem with room on it, or
/// one a reboot does not empty.
///
/// A directory of ours is one we answer for, so it is checked before it is used:
/// the system's temporary directory is shared with everybody logged in, and a
/// name under it is a name somebody else may have made first. A directory the
/// config named is used exactly as the user made it — where they put it and who
/// may write there is their business, not ours to second-guess.
///
/// Three things want it: a zipped nodelist, a zipped echolist, and the file a
/// message is handed over in where `external_editor` names one.
///
/// Called by whoever needs the directory, at the moment they need it: nothing is
/// made by a config that merely mentions one. A failure says what is wrong with
/// the directory and what to set instead — the caller is left to say what it
/// wanted the directory for.
[[nodiscard]] tl::expected<std::string, ErrorPtr> makeTempDir(
    const std::string& configured);

}  // namespace amberedit::config
