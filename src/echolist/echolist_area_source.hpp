#pragma once

#include <memory>
#include <string>

#include "config/app_config.hpp"
#include "ports/i_area_source.hpp"

namespace amberedit::echolist {

/// The area list with the descriptions the compiled echolist carries laid over
/// it.
///
/// A wrapper round whatever the areas came from rather than something
/// `AreaManager` knows about: the manager knows one `IAreaConfigSource`, and
/// where a description comes from is nobody's business above this. It sits
/// outside the core for the same reason the nodelist does — the core knows
/// neither subsystem is there.
///
/// **Where the description goes is `AreaConfig::description`**, which is the one
/// place anything reads: the area list's `d` column and the `@CDESC`/`@ODESC`
/// template tokens both. An echolist therefore describes an echo everywhere the
/// tosser config would have.
///
/// The compiled file is opened afresh on every `loadAreas()`, which is startup
/// and each Ctrl-R: an echolist recompiled since is then read, and the file is a
/// few hundred kilobytes read once per rescan. **A compiled echolist that is not
/// there, or will not open, leaves every description exactly as it was** — it is
/// a convenience, like the nodelist, and there is no version of "your echolist
/// is missing" worth an empty area list.
class EcholistAreaSource final : public ports::IAreaConfigSource {
public:
    /// `dbPath` is `echolist_db`. Empty is allowed and means every area keeps
    /// the description it came with, so that the one wrapper covers the config
    /// that names no echolist too.
    EcholistAreaSource(std::unique_ptr<ports::IAreaConfigSource> inner,
                       std::string dbPath, config::DescriptionPriority priority);

    /// Throws only what `inner` throws — the tosser config being unreadable is
    /// still fatal, and nothing about an echolist is.
    std::vector<domain::AreaConfig> loadAreas() override;

private:
    std::unique_ptr<ports::IAreaConfigSource> inner_;
    std::string dbPath_;
    config::DescriptionPriority priority_;
};

/// `inner` with the echolist descriptions over it where the config names a
/// compiled echolist, and `inner` itself where it does not.
///
/// Unwrapped where there is nothing to add, so that the common config — which
/// has no echolist — reaches the same source it always did.
[[nodiscard]] std::unique_ptr<ports::IAreaConfigSource> withEcholistDescriptions(
    std::unique_ptr<ports::IAreaConfigSource> inner, const config::AppConfig& cfg);

}  // namespace amberedit::echolist
