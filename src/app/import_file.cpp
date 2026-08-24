#include "app/import_file.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "app/copy_commands.hpp"
#include "config/text_util.hpp"
#include "encoding/charset_detector.hpp"
#include "encoding/iconv_recoder.hpp"

namespace amberedit::app {
namespace {

/// Bytes one uuencoded line carries. Forty-five is what the encoding has always
/// used: three bytes to four characters, and sixty characters of data is a line
/// no mailer has ever had trouble with.
constexpr size_t kBytesPerLine = 45;

/// Where a tab lands, opened out. Eight columns is what a tab has meant since
/// the teletype, and it is what the file was written against.
constexpr size_t kTabStop = 8;

/// One line of the file as a message can carry it: tabs opened out to the next
/// stop, and every other control byte dropped.
///
/// A NUL is the one that matters — FTS-0001 ends a message at the first one, so
/// a single byte of a file read as text could cut the message off — but none of
/// them is anything a reader would show, and a file imported as text is being
/// imported to be read. What is dropped is judged byte by byte, which is safe
/// on UTF-8: every byte of a multi-byte character has its high bit set.
std::string sanitize(std::string_view line) {
    std::string out;
    out.reserve(line.size());
    for (const char byte : line) {
        if (byte == '\t') {
            out.append(kTabStop - (out.size() % kTabStop), ' ');
            continue;
        }
        if (static_cast<unsigned char>(byte) < 0x20 || byte == 0x7F) continue;
        out += byte;
    }
    return out;
}

/// The charset as iconv is to be asked for it. FTN names its charsets its own
/// way — `+7_FIDO` and `866` are both CP866 — and a user typing one into the
/// dialog means the same thing by it as a CHRS kludge does, so the same table
/// answers both. A name that identifies no particular encoding, `IBMPC` being
/// the one in use, is left as it was typed for iconv to refuse by name.
std::string charsetFor(const std::string& named) {
    const std::string normalized = encoding::CharsetDetector::normalize(named);
    return normalized.empty() ? named : normalized;
}

tl::expected<std::vector<std::string>, ErrorPtr> importText(
    const ImportRequest& request) {
    auto bytes = config::text::readFile(request.path);
    if (!bytes) return tl::make_unexpected(std::move(bytes).error());

    // intoUtf8 and not the reader's toUtf8: that one hands the bytes back
    // unchanged, which is right for a message being read and wrong here — the
    // charset was typed a moment ago, and a mistyped one would go into the
    // message as mojibake nobody could undo afterwards.
    encoding::IconvRecoder recoder;
    auto utf8 = recoder.intoUtf8(*bytes, charsetFor(request.charset));
    if (!utf8) return tl::make_unexpected(std::move(utf8).error());

    const std::vector<std::string> read = config::text::splitLines(*utf8);

    std::vector<std::string> lines;
    lines.reserve(read.size() + 2);
    if (!request.beginLine.empty()) lines.push_back(request.beginLine);
    // A `CC:` or `XC:` line of the file is disarmed on the way in: the file is
    // being read into the message as text, and a command in it is one the
    // writer of the file asked for rather than the writer of the message.
    for (const auto& line : read) {
        lines.push_back(disarmCopyCommand(sanitize(line)));
    }
    if (!request.endLine.empty()) lines.push_back(request.endLine);
    return lines;
}

tl::expected<std::vector<std::string>, ErrorPtr> importUue(const ImportRequest& request) {
    auto bytes = config::text::readFile(request.path);
    if (!bytes) return tl::make_unexpected(std::move(bytes).error());

    // The name the file goes out under, and nothing of the path it was read
    // from: where it stood on this machine is nobody else's business, and
    // uudecode would make a directory of it at the other end.
    const std::string name = std::filesystem::path(request.path).filename().string();
    return uuencode(*bytes, name);
}

/// One six-bit group as a uuencoded character.
char encodeSix(unsigned value) {
    value &= 0x3Fu;
    // Zero is a backquote rather than the space the original encoding wrote:
    // the two decode alike, and a space at the end of a line does not survive
    // being mailed.
    return value == 0 ? '`' : static_cast<char>(value + 0x20u);
}

}  // namespace

std::vector<std::string> uuencode(std::string_view bytes, std::string_view name) {
    std::vector<std::string> lines;
    // The data, and the three lines round it.
    lines.reserve(((bytes.size() + kBytesPerLine - 1) / kBytesPerLine) + 3);
    // 644 is what uuencode writes for a file with no mode worth carrying, and
    // what every FTN editor has always put there.
    lines.push_back("begin 644 " + std::string(name));

    for (size_t at = 0; at < bytes.size(); at += kBytesPerLine) {
        const size_t take = std::min(kBytesPerLine, bytes.size() - at);
        std::string line(1, encodeSix(static_cast<unsigned>(take)));

        for (size_t i = 0; i < take; i += 3) {
            const auto byteAt = [&](size_t offset) -> unsigned {
                const size_t index = i + offset;
                return index < take ? static_cast<unsigned char>(bytes[at + index]) : 0u;
            };
            const unsigned first = byteAt(0);
            const unsigned second = byteAt(1);
            const unsigned third = byteAt(2);

            // Three bytes to four characters, whether or not all three are
            // there: the length character at the head of the line is what says
            // how many of them count.
            line += encodeSix(first >> 2);
            line += encodeSix(((first & 0x03u) << 4) | (second >> 4));
            line += encodeSix(((second & 0x0Fu) << 2) | (third >> 6));
            line += encodeSix(third);
        }
        lines.push_back(line);
    }

    // The zero-length line that closes the data, and then the word that closes
    // the file. Both are what uudecode looks for.
    lines.emplace_back("`");
    lines.emplace_back("end");
    return lines;
}

tl::expected<std::vector<std::string>, ErrorPtr> importFile(
    const ImportRequest& request) {
    return request.mode == ImportMode::Uue ? importUue(request) : importText(request);
}

}  // namespace amberedit::app
