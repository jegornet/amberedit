#include "ui/extern_util.hpp"

#include <cstddef>

#include "ui/keys.hpp"

namespace amberedit::ui::extern_util {

bool handleKey(AppState& state, const term::Event& event, CommandScreen screen) {
    for (size_t slot = 0; slot < config::kExternUtilCount; ++slot) {
        const auto command = Commands::externUtilOn(screen, slot);
        if (!command || !state.keys.is(event, *command)) continue;
        return run(state, *command);
    }
    return false;
}

bool run(AppState& state, Command command) {
    const auto slot = Commands::externUtilOf(command);
    // A slot with no program behind it is not a utility: the config refuses one
    // in a menu or a hint list and `main()` refuses a key bound to it, so this
    // is the belt to those braces and not a case anybody reaches.
    if (!slot || !state.config.externUtils[*slot].isSet()) return false;
    state.externUtilRequested = slot;
    return true;
}

}  // namespace amberedit::ui::extern_util
