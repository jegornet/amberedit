#pragma once

#include <optional>
#include <string>
#include <string_view>

/// Base64, RFC 4648's standard alphabet with `=` padding — the spelling OSC 52
/// carries a clipboard's text in, both ways across the terminal.
namespace amberedit::encoding {

/// The bytes as base64, padded to a multiple of four.
[[nodiscard]] std::string toBase64(std::string_view bytes);

/// The bytes base64 stands for, or nothing where it is not base64.
///
/// Padding may be left off, since not every terminal writes it; anything else
/// outside the alphabet — a stray byte, a line break, a `=` in the middle — is
/// refused rather than skipped. What is decoded here is put into a message, and
/// a reply half garbled by a terminal is better refused than pasted.
[[nodiscard]] std::optional<std::string> fromBase64(std::string_view text);

}  // namespace amberedit::encoding
