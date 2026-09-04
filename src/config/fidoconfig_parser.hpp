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
    explicit FidoconfigParser(std::string path, PathMap paths = {});

    [[nodiscard]] tl::expected<std::vector<domain::AreaConfig>, ErrorPtr> loadAreas()
        override;

    /// Parsing from a string — the entry point for tests and include files.
    static std::vector<domain::AreaConfig> parseText(const std::string& content,
                                                     const PathMap& paths = {});

private:
    std::string path_;
    PathMap paths_;
};

}  // namespace amberedit::config
