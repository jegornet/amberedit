#pragma once

#include "app/area_manager.hpp"
#include "config/app_config.hpp"
#include "ui/keys.hpp"

namespace amberedit::ui {

/// Runs the TUI on top of a ready AreaManager and returns the process exit
/// code. Errors inside the loop do not bring the app down: a frame that cannot
/// be drawn says so in place of the screen, and a keystroke that throws leaves
/// the state as it was.
///
/// `keys` is the layout every screen answers keystrokes against —
/// `KeyMap::defaults()` where the config names no `keys` file.
int runApp(app::AreaManager& manager, const config::AppConfig& config,
           const KeyMap& keys);

}  // namespace amberedit::ui
