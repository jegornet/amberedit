#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "config/app_config.hpp"
#include "domain/ftn_address.hpp"
#include "support/result.hpp"

/// What the setup wizard knows that has nothing to do with a terminal: what a
/// good answer looks like, and what to put in a field before anybody has typed
/// in it.
///
/// Apart so that it can be tested. The wizard's own file draws and dispatches
/// and is hard to ask a question of; these are ordinary functions over strings,
/// and they are where every rule that could be wrong lives.
namespace amberedit::ui::setup {

/// The name a message will be written under. Anything but nothing, and nothing
/// the config format cannot carry.
[[nodiscard]] Result<void> checkName(std::string_view typed);

/// The address, as `FtnAddress` reads one: `2:5020/9999` or `2:5020/9999.1`.
[[nodiscard]] Result<domain::FtnAddress> checkAddress(std::string_view typed);

/// A charset iconv on this machine knows, written as the config takes it —
/// which is one word, since `default_charset` and `compose_charset` are read as
/// a single value.
[[nodiscard]] Result<void> checkCharsetAnswer(std::string_view typed);

/// The charset a config for this address most likely wants to read messages in
/// where the message itself does not say.
///
/// CP866 for the ex-USSR of zone 2 — an address whose normalized spelling starts
/// `2:50` or `2:60` — LATIN-1 for the rest of zone 2, CP437 elsewhere. It is a
/// guess with a field to correct it in and not a rule: what it saves is the
/// reader who does not yet know that a Fidonet message says its charset in a
/// kludge, and that old Russian mail carries none.
[[nodiscard]] std::string defaultReadCharset(const domain::FtnAddress& address);

/// The tosser config, read the way AmberEdit will read it at every start: it is
/// there, it is a file, and the parser for the format gets areas out of it.
/// Answers with how many, which is worth saying — a config that parsed to no
/// areas at all is the wrong file or the wrong format, and both are better found
/// out here than at the first empty area list.
[[nodiscard]] Result<size_t> checkTosserConfig(const std::string& path,
                                               config::TosserConfigFormat format);

/// Whether the file picker shows a file when it is looking for this format's
/// config: everything for fidoconfig, whose config is called whatever the sysop
/// called it, and the one name for the two formats that have one.
[[nodiscard]] bool acceptsTosserFile(config::TosserConfigFormat format,
                                     std::string_view name);

/// Where the compiled nodelist goes when nobody says otherwise: `amberndl.db`
/// beside the nodelist it is compiled from.
[[nodiscard]] std::string defaultNodelistDb(const std::string& nodelistPath);

/// The installed `default.tpl`, looked for where an install would have put one:
/// the two usual prefixes, the working directory, and beside the program and its
/// `../share/amberedit` — which is what a build tree and a relocated install
/// look like. Absolute in every case, since a config naming a relative path
/// would have it read against whatever directory AmberEdit is next started in.
[[nodiscard]] std::optional<std::string> probeTemplate(const std::string& programPath);

/// The template the config will name: the installed one where there is one, and
/// otherwise a copy of the one in the binary, written beside the config.
///
/// Written before the config is, so that a config never names a template that
/// was not written. A `default.tpl` already sitting beside the config and not
/// readable is refused rather than replaced — it is somebody's, and this is the
/// one thing here that touches a file that was already there.
[[nodiscard]] Result<std::string> ensureTemplate(const std::string& configPath,
                                                 const std::string& programPath);

/// Where the config may be written: nothing is there, the directory above it is,
/// and it is not itself a directory.
[[nodiscard]] Result<void> checkTargetPath(const std::string& path);

/// `/home/vasya/ftn/etc/areas` written back as `~/ftn/etc/areas`, which is how
/// the sample config writes a path under the home directory and what survives
/// that directory being mounted somewhere else. Only a leading `~/` is what the
/// config expands, so only a path strictly under `$HOME` is abbreviated.
[[nodiscard]] std::string abbreviateHome(const std::string& path);

}  // namespace amberedit::ui::setup
