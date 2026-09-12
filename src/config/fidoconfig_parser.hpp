#pragma once

#include <string>
#include <vector>

#include "config/path_map.hpp"
#include "ports/i_area_source.hpp"

namespace amberedit::config {

/// Parser for husky/hpt-style tosser configs (fidoconfig):
///
///   EchoArea localnet /ftn/msg/localnet -b squish -a 2:5020/1
///   netmailarea NETMAIL /ftn/msg/netmail -g A -b msg
///
/// It reads EchoArea / NetmailArea / LocalArea / BadArea / DupeArea lines, the
/// `echoareadefaults` those inherit from, the `set` definitions that `[name]`
/// stands for anywhere below them, and the include directive. Everything else
/// is ignored: AmberEdit only needs the area list and does not aim to
/// understand the whole tosser config.
///
/// Every path it takes out of the file — an area's base and the file an
/// `include` names — comes back through the `map_path` rules it was built with,
/// so a config written for a tosser that runs elsewhere opens here.
class FidoconfigParser final : public ports::IAreaConfigSource {
public:
    /// `charset` is `config_charset` — the charset the AmberEdit config, and
    /// so the tosser config it names, is written in. Empty means UTF-8.
    explicit FidoconfigParser(std::string path, PathMap paths = {},
                              std::string charset = {});

    [[nodiscard]] tl::expected<std::vector<domain::AreaConfig>, ErrorPtr> loadAreas()
        override;

    /// Parsing from a string — the entry point for tests and include files.
    static std::vector<domain::AreaConfig> parseText(const std::string& content,
                                                     const PathMap& paths = {});

    /// Every `include` the last `loadAreas()` passed over because the file it
    /// named is not there, in the spelling it was looked for under — mapped by
    /// `map_path` and resolved against the including file's directory, which is
    /// where a reader has to go and look.
    ///
    /// Reading goes on past such a line rather than stopping at it: a config
    /// whose optional include is missing still names areas, and a start that
    /// refused it would be AmberEdit refusing to run over a file it does not
    /// need. So the fact is kept here instead, for whoever has somebody in
    /// front of them to say out loud — `checkTosserConfig()` does, and "no
    /// areas at all" is exactly the shape this failure takes.
    [[nodiscard]] const std::vector<std::string>& missingIncludes() const {
        return missingIncludes_;
    }

private:
    std::string path_;
    PathMap paths_;
    std::string charset_;
    std::vector<std::string> missingIncludes_;
};

}  // namespace amberedit::config
