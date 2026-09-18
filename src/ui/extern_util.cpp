#include "ui/extern_util.hpp"

#include <cstddef>
#include <vector>

#include "app/external_editor.hpp"
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
    state.externUtilRequested = command;
    return true;
}

bool handsOverMessage(const AppState& state, Command command) {
    const auto slot = Commands::externUtilOf(command);
    if (!slot) return false;
    if (!app::namesMessageFile(state.config.externUtils[*slot].command)) return false;
    return Commands::of(command).screen != CommandScreen::AreaList;
}

std::vector<std::string> messageFor(const AppState& state, Command command) {
    const CommandScreen screen = Commands::of(command).screen;
    if (screen == CommandScreen::Compose) return state.edit.lines;
    if (screen != CommandScreen::Reader || !state.readBody) return {};
    std::vector<std::string> lines;
    for (const auto& line : state.readBody->lines) {
        if (!line.kludge) lines.push_back(line.text);
    }
    return lines;
}

}  // namespace amberedit::ui::extern_util
