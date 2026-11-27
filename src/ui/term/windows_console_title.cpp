#include "ui/term/windows_console_title.hpp"

#include <windows.h>

#include <string>

namespace amberedit::ui::term {

namespace {

/// What the console was called before the first title went up.
std::wstring saved;

}  // namespace

void saveConsoleTitle() {
    // The title is at most 32K characters by the documentation of the call;
    // a buffer that size answers once rather than growing and asking again.
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetConsoleTitleW(buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    saved = std::move(buffer);
}

void setConsoleTitle(std::string_view title) {
    std::wstring wide;
    if (!title.empty()) {
        const int size = MultiByteToWideChar(CP_UTF8, 0, title.data(),
                                             static_cast<int>(title.size()), nullptr, 0);
        wide.assign(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, title.data(), static_cast<int>(title.size()),
                            wide.data(), size);
    }
    SetConsoleTitleW(wide.c_str());
}

void restoreConsoleTitle() { SetConsoleTitleW(saved.c_str()); }

}  // namespace amberedit::ui::term
