#pragma once

#include <string>
#include <vector>

#include "ports/i_area_source.hpp"

namespace amberedit::config {

/// Parser for Squish's own squish.cfg:
///
///   EchoArea localnet /ftn/msg/localnet -$ -$gB -p192:168/1 192:168/2
///   NetArea  NETMAIL  /ftn/msg/netmail  -$gA -p2:382/736
///   EchoArea su.general passthrough -0 -$gA -p2:382/736 2:5020/715
///
/// It reads EchoArea / NetArea / LocalArea / BadArea / DupeArea lines. Options
/// carry their value attached rather than as a separate token: `-$` selects a
/// Squish base and its absence means Fido *.msg, `-$g` gives the group, `-p`
/// the area's AKA. Bare addresses after those are links. Anything else
/// starting with '-' belongs to the tosser and is skipped.
///
/// JAM is deliberately absent: Squish's own configuration cannot describe it.
class SquishCfgParser final : public ports::IAreaConfigSource {
public:
    explicit SquishCfgParser(std::string path);

    std::vector<domain::AreaConfig> loadAreas() override;

    /// Parsing from a string — the entry point for tests.
    static std::vector<domain::AreaConfig> parseText(const std::string& content);

private:
    std::string path_;
};

}  // namespace amberedit::config
