#pragma once

#include <string_view>

namespace amberedit::ui::term {

/// The Windows console's title, for `Terminal::setTitle()`. A file of its own
/// because it wants `<windows.h>`, which does not sit beside `curses.h`: both
/// define `MOUSE_MOVED`. Built on Windows alone.
///
/// The console keeps no stack of titles the way xterm does, so the one it had
/// is read and held here until it is put back.

/// Reads the title the console has now, for `restoreConsoleTitle()`.
void saveConsoleTitle();

/// Puts `title`, UTF-8, in the console's title bar.
void setConsoleTitle(std::string_view title);

/// Puts back the title `saveConsoleTitle()` read.
void restoreConsoleTitle();

}  // namespace amberedit::ui::term
