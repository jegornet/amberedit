#pragma once

#include <string>
#include <vector>

#include "ports/i_area_source.hpp"

namespace amberedit::config {

/// Parser for husky/hpt-style tosser configs (fidoconfig):
///
///   EchoArea localnet /ftn/msg/localnet -b squish -a 2:5020/1
///   netmailarea NETMAIL /ftn/msg/netmail -g A -b msg
///
/// It reads EchoArea / NetmailArea / LocalArea / BadArea / DupeArea lines and
/// the include directive. Everything else is ignored: AmberEdit only needs the
/// area list and does not aim to understand the whole tosser config.
class FidoconfigParser final : public ports::IAreaConfigSource {
public:
    explicit FidoconfigParser(std::string path);

    [[nodiscard]] tl::expected<std::vector<domain::AreaConfig>, ErrorPtr> loadAreas()
        override;

    /// Parsing from a string — the entry point for tests and include files.
    static std::vector<domain::AreaConfig> parseText(const std::string& content);

private:
    std::string path_;
};

}  // namespace amberedit::config
