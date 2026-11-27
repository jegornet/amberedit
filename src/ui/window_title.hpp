#pragma once

#include <string>

#include "ui/app_state.hpp"

namespace amberedit::ui {

/// What the window's title says while `state` is in front of the user: the
/// screen, with the area it is about.
///
/// The screen and nothing over it. A box is something asked on the way and
/// goes again; a title that changed under every confirmation would flicker in
/// a tab bar for no news.
[[nodiscard]] std::string windowTitle(const AppState& state);

}  // namespace amberedit::ui
