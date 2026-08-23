#pragma once

#include <string>

namespace amberedit::ui::setup {

/// Runs the setup wizard on a terminal of its own and says how it went: 0 where
/// a config was written, 1 where the user left without one, 2 where there is no
/// terminal to ask in.
///
/// Its own terminal because there is nothing else to borrow: `runApp` is built
/// out of a config, an area manager and a key map, and the whole point of this
/// is that there is no config yet. `programPath` is argv[0], which is one of the
/// places the installed message template is looked for.
[[nodiscard]] int runSetup(const std::string& programPath);

}  // namespace amberedit::ui::setup
