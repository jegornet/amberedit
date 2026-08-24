#include "app/export_file.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "config/text_util.hpp"
#include "domain/ftn_address.hpp"
#include "domain/message.hpp"
#include "encoding/iconv_recoder.hpp"

namespace amberedit::app {
namespace {

namespace fs = std::filesystem;

/// The label column of the header block, as the reader draws it: "From : ",
/// four characters and the colon, so a file reads the way the screen did.
std::string labelled(const std::string& label, const std::string& value) {
    std::string row = label;
    row.append(4 - std::min<size_t>(4, label.size()), ' ');
    return row + " : " + value;
}

/// The address in brackets after a name, or nothing where the base kept none —
/// a JAM echo area stores no sender address at all.
std::string withAddress(const std::string& name, const domain::FtnAddress& address) {
    return address.isValid() ? name + " (" + address.toString() + ")" : name;
}

// --- the files a message carries ---------------------------------------------

/// Bytes one uuencoded line carries at most — the encoding's own 45, which is
/// also the largest number its length character can state.
constexpr size_t kBytesPerLine = 45;

/// The six bits a uuencoded character stands for, or -1 where it is not one.
///
/// Space and backquote both mean zero: the original encoding wrote a space and
/// every FTN encoder since has written a backquote, because mail strips a
/// trailing space and the line would arrive a byte short.
int decodeSix(char c) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte < 0x20 || byte > 0x60) return -1;
    return static_cast<int>((byte - 0x20u) & 0x3Fu);
}

/// One data line decoded, or nothing where it is not one.
///
/// The line is padded with zeros to the length its first character states rather
/// than being required to carry them: an encoder that wrote zero as a space has
/// had those spaces stripped somewhere along the way, and the bytes they stood
/// for are zeros either way. Anything *longer* than the length states is refused
/// — that is not a line this encoding wrote, and guessing at what it is would
/// turn a line of somebody's text into a file.
std::optional<std::string> decodeUueLine(std::string_view line) {
    const int length = decodeSix(line[0]);
    if (length < 0 || static_cast<size_t>(length) > kBytesPerLine) return std::nullopt;

    const auto count = static_cast<size_t>(length);
    // Three bytes to four characters, whether or not all three are there.
    const size_t needed = ((count + 2) / 3) * 4;
    std::string data(line.substr(1));
    if (data.size() > needed) return std::nullopt;
    for (const char c : data) {
        if (decodeSix(c) < 0) return std::nullopt;
    }
    data.append(needed - data.size(), '`');

    std::string bytes;
    bytes.reserve(count);
    for (size_t i = 0; i + 3 < data.size(); i += 4) {
        const auto first = static_cast<unsigned>(decodeSix(data[i]));
        const auto second = static_cast<unsigned>(decodeSix(data[i + 1]));
        const auto third = static_cast<unsigned>(decodeSix(data[i + 2]));
        const auto fourth = static_cast<unsigned>(decodeSix(data[i + 3]));

        bytes += static_cast<char>((first << 2) | (second >> 4));
        bytes += static_cast<char>(((second & 0x0Fu) << 4) | (third >> 2));
        bytes += static_cast<char>(((third & 0x03u) << 6) | fourth);
    }
    // The whole groups are decoded and then cut back to what the line said it
    // carried: the last group of a line is padded, and its padding is not data.
    bytes.resize(count);
    return bytes;
}

/// The name a `begin` line gives the file, or nothing where the line is not one.
///
/// `begin <mode> <name>`, the mode three or four octal digits — 644 is what
/// every encoder writes, and a leading zero is what some of them write it with.
/// The name is the rest of the line, so a name with spaces in it survives.
///
/// It is then taken as a **name and not a path**: what stands in the message
/// came off somebody else's machine, and a `../` in it would write outside the
/// directory the user picked. DOS separators are cut as well as this system's —
/// `C:\DL\FILE.ZIP` is a name FTN mail has carried since there was FTN mail.
std::optional<std::string> uueBeginName(std::string_view line) {
    const std::string_view trimmed = config::text::trim(line);
    if (!config::text::startsWith(trimmed, "begin ")) return std::nullopt;

    std::string_view rest = config::text::trim(trimmed.substr(6));
    size_t digits = 0;
    while (digits < rest.size() && rest[digits] >= '0' && rest[digits] <= '7') ++digits;
    if (digits < 3 || digits > 4) return std::nullopt;
    if (digits >= rest.size() || !config::text::asciiIsSpace(rest[digits])) {
        return std::nullopt;
    }

    std::string name(config::text::trim(rest.substr(digits)));
    const size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name.erase(0, slash + 1);
    if (name.empty() || name == "." || name == "..") return std::nullopt;
    return name;
}

bool isUueEnd(std::string_view line) {
    return config::text::iequals(config::text::trim(line), "end");
}

/// The block that begins after `at`, or nothing where what stands there is not
/// one. `at` is left past the `end` line when a file was read out of it, and
/// untouched otherwise — the caller goes on looking from the line after the
/// `begin`, since a block that came to nothing may still have a real one under
/// it.
std::optional<std::string> readUueBlock(const std::vector<std::string>& lines,
                                        size_t& at) {
    std::string bytes;
    // Whether the data is over: the zero-length line that closes it, or a blank
    // line where the mailer took that line's backquote off. Nothing but `end`
    // may follow, so a blank line in the middle of the data is a block that has
    // been damaged rather than one to be decoded as far as the damage.
    bool closed = false;

    for (size_t i = at; i < lines.size(); ++i) {
        const std::string_view line = config::text::trim(lines[i]);
        if (isUueEnd(line)) {
            at = i + 1;
            return bytes;
        }
        if (line.empty()) {
            closed = true;
            continue;
        }
        if (closed) return std::nullopt;

        const auto decoded = decodeUueLine(line);
        if (!decoded) return std::nullopt;
        if (decoded->empty()) {
            closed = true;
            continue;
        }
        bytes += *decoded;
    }
    // The message ends in the middle of the file: one section of a file split
    // across several messages, which is not a file this reader can put together.
    return std::nullopt;
}

}  // namespace

std::vector<std::string> exportedLines(const domain::MessageHeader& header,
                                       const domain::MessageBody& body,
                                       const std::string& dateFormat) {
    std::vector<std::string> lines;
    lines.reserve(body.lines.size() + 6);

    // The header block the reader draws, field for field, and the rule that
    // closes it off — which is also what keeps one message apart from the next
    // where several are exported into the same file.
    lines.push_back(labelled("From", withAddress(header.from, header.origAddr)));
    lines.push_back(labelled("To", withAddress(header.to, header.destAddr)));
    lines.push_back(labelled("Subj", header.subject));
    lines.push_back(labelled("Date", header.date.format(dateFormat, header.utcOffset)));
    lines.emplace_back(72, '-');

    for (const auto& line : body.lines) {
        if (!line.kludge) lines.push_back(line.text);
    }
    return lines;
}

tl::expected<void, ErrorPtr> exportMessage(const ExportRequest& request,
                                           const domain::MessageHeader& header,
                                           const domain::MessageBody& body) {
    encoding::IconvRecoder recoder;

    // A charset the file cannot be written in is a failure rather than a file
    // full of question marks: it was named a moment ago, and what is on disk
    // afterwards cannot be told from a message that had them in it. That is why
    // this goes through intoCharset and not the reader's fromUtf8.
    std::string out;
    for (const auto& line : exportedLines(header, body, request.dateFormat)) {
        auto encoded = recoder.intoCharset(line, request.charset);
        if (!encoded) return tl::make_unexpected(std::move(encoded).error());
        out += *encoded;
        out += '\n';
    }

    // Added to or written afresh as the caller was told to ask, and the stream
    // is asked afterwards whether it took it: a full disk fails on the write and
    // not on the open.
    const auto how =
        request.write == ExportWrite::Overwrite ? std::ios::trunc : std::ios::app;
    std::ofstream file(request.path, std::ios::binary | how);
    if (!file) return failure("cannot write file: " + request.path);
    file.write(out.data(), static_cast<std::streamsize>(out.size()));
    file.flush();
    if (!file) return failure("cannot write file: " + request.path);
    return {};
}

std::vector<UueFile> uueFilesIn(const std::vector<std::string>& lines) {
    std::vector<UueFile> files;
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto name = uueBeginName(lines[i]);
        if (!name) continue;

        size_t after = i + 1;
        const auto bytes = readUueBlock(lines, after);
        if (!bytes) continue;

        files.push_back(UueFile{*name, *bytes});
        // Past the `end`, so that the next file is looked for where the last one
        // stopped rather than inside it.
        i = after - 1;
    }
    return files;
}

std::vector<UueFile> uueFiles(const domain::MessageBody& body) {
    std::vector<std::string> lines;
    lines.reserve(body.lines.size());
    // The message as it was written, the service lines left out exactly as the
    // text export leaves them out: a kludge is this network's business, and none
    // of them is part of a file.
    for (const auto& line : body.lines) {
        if (!line.kludge) lines.push_back(line.text);
    }
    return uueFilesIn(lines);
}

tl::expected<void, ErrorPtr> saveUueFiles(const std::string& directory,
                                          const std::vector<UueFile>& files) {
    if (files.empty()) return failure("nothing to save");

    // Every name is looked at before any of them is written. These names are the
    // message's rather than the user's and there is nowhere to change one, so a
    // file already standing under one of them is the thing that cannot be
    // undone — and a directory left half filled would be a second one.
    for (const auto& file : files) {
        const fs::path path = fs::path(directory) / file.name;
        std::error_code ec;
        if (fs::exists(path, ec)) return failure("file exists: " + file.name);
    }

    for (const auto& file : files) {
        const fs::path path = fs::path(directory) / file.name;
        // Asked afterwards whether it took it: a full disk fails on the write
        // and not on the open.
        std::ofstream out(path.string(), std::ios::binary | std::ios::trunc);
        if (!out) return failure("cannot write file: " + file.name);
        out.write(file.bytes.data(), static_cast<std::streamsize>(file.bytes.size()));
        out.flush();
        if (!out) return failure("cannot write file: " + file.name);
    }
    return {};
}

}  // namespace amberedit::app
