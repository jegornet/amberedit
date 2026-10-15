#pragma once

#include <string>

namespace amberedit::sys {

/// The system clipboard of Windows, the one system with a clipboard a program
/// can simply ask for. Everywhere else a clipboard is a desktop's — an X
/// server's selection, a Wayland compositor's, the macOS pasteboard — and is
/// reached through the program that desktop provides, which is what
/// `clipboard external` is for.

/// What a call came to: the text where one was read, and the system's own words
/// for what went wrong where anything did. An empty `reason` is success.
struct ClipboardResult {
    std::string text;
    std::string reason;
};

/// Whether there is a system clipboard here to ask at all: true on Windows and
/// false everywhere else, where both calls below answer with a reason.
[[nodiscard]] bool hasSystemClipboard();

/// Puts UTF-8 text on the system clipboard as Unicode text, line breaks written
/// the way that system writes them.
[[nodiscard]] ClipboardResult setSystemClipboard(const std::string& utf8);

/// The Unicode text on the system clipboard, as UTF-8, and nothing — not a
/// failure — where it holds no text.
[[nodiscard]] ClipboardResult getSystemClipboard();

}  // namespace amberedit::sys
