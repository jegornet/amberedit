#include "sys/clipboard.hpp"

#ifdef _WIN32
#include <windows.h>

#include <cstring>
#endif

namespace amberedit::sys {

#ifdef _WIN32

namespace {

std::string lastErrorText() {
    const DWORD code = ::GetLastError();
    LPSTR text = nullptr;
    const DWORD length =
        ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                             FORMAT_MESSAGE_IGNORE_INSERTS,
                         nullptr, code, 0, reinterpret_cast<LPSTR>(&text), 0, nullptr);
    if (length == 0 || text == nullptr) return "error " + std::to_string(code);

    std::string message(text, length);
    ::LocalFree(text);
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n' ||
                                message.back() == ' ' || message.back() == '.')) {
        message.pop_back();
    }
    return message;
}

/// The clipboard, opened for as long as one is held: another program cannot
/// open it meanwhile, so it is given back however the call leaves.
class HeldClipboard {
public:
    HeldClipboard() : open_(::OpenClipboard(nullptr) != 0) {}
    ~HeldClipboard() {
        if (open_) ::CloseClipboard();
    }

    HeldClipboard(const HeldClipboard&) = delete;
    HeldClipboard& operator=(const HeldClipboard&) = delete;
    HeldClipboard(HeldClipboard&&) = delete;
    HeldClipboard& operator=(HeldClipboard&&) = delete;

    [[nodiscard]] bool isOpen() const { return open_; }

private:
    bool open_;
};

/// UTF-8 with every line ending in CR LF, as UTF-16: the shape a Windows
/// program expects of text it is pasted.
std::wstring windowsText(const std::string& utf8) {
    std::string lines;
    lines.reserve(utf8.size() + (utf8.size() / 32));
    for (const char c : utf8) {
        if (c == '\r') continue;
        if (c == '\n') lines += '\r';
        lines += c;
    }
    if (lines.empty()) return {};
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, lines.data(),
                                             static_cast<int>(lines.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring wide(static_cast<size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, lines.data(), static_cast<int>(lines.size()),
                          wide.data(), length);
    return wide;
}

std::string utf8Of(const wchar_t* wide) {
    const int length =
        ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string out(static_cast<size_t>(length), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), length, nullptr, nullptr);
    out.pop_back();  // the terminating zero the length counted
    return out;
}

}  // namespace

bool hasSystemClipboard() {
    return true;
}

ClipboardResult setSystemClipboard(const std::string& utf8) {
    const std::wstring wide = windowsText(utf8);
    const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);

    // The memory is handed to the clipboard once it is set, and is ours to free
    // only where that did not happen.
    HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr) return {{}, lastErrorText()};
    void* at = ::GlobalLock(memory);
    if (at == nullptr) {
        const std::string why = lastErrorText();
        ::GlobalFree(memory);
        return {{}, why};
    }
    std::memcpy(at, wide.c_str(), bytes);
    ::GlobalUnlock(memory);

    const HeldClipboard clipboard;
    if (!clipboard.isOpen() || ::EmptyClipboard() == 0 ||
        ::SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        const std::string why = lastErrorText();
        ::GlobalFree(memory);
        return {{}, why};
    }
    return {};
}

ClipboardResult getSystemClipboard() {
    const HeldClipboard clipboard;
    if (!clipboard.isOpen()) return {{}, lastErrorText()};
    // Nothing that is text is no text at all, and not a failure.
    HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
    if (data == nullptr) return {};
    const auto* wide = static_cast<const wchar_t*>(::GlobalLock(data));
    if (wide == nullptr) return {{}, lastErrorText()};
    std::string text = utf8Of(wide);
    ::GlobalUnlock(data);
    return {std::move(text), {}};
}

#else

namespace {

constexpr const char* kNone = "this system has no clipboard a program can ask for";

}  // namespace

bool hasSystemClipboard() {
    return false;
}

ClipboardResult setSystemClipboard(const std::string& /*utf8*/) {
    return {{}, kNone};
}

ClipboardResult getSystemClipboard() {
    return {{}, kNone};
}

#endif

}  // namespace amberedit::sys
