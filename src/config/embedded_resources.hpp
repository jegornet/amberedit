#pragma once

#include <string_view>

namespace amberedit::config::resources {

/// `amberedit.cfg.example` as the build put it into the binary.
///
/// It is in there because it cannot be read off disk when it is wanted: the
/// sample config is documentation, and the install rules ship the binary, the
/// template and the themes and leave it out — every package format has its own
/// place for documentation, and none of them is one the program may count on.
/// `--setup` writes a config by filling this in, so it has to be here.
[[nodiscard]] std::string_view exampleConfig();

/// `default.tpl`, for the same reason from the other end: it *is* installed, but
/// only where AmberEdit was installed, and `--setup` may well be the first thing
/// somebody runs out of a build tree. Where no installed copy answers, the
/// wizard writes this one out beside the config it is writing.
[[nodiscard]] std::string_view defaultTemplate();

}  // namespace amberedit::config::resources
