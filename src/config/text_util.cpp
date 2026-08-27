#include "config/text_util.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace amberedit::config::text {

tl::expected<void, ErrorPtr> insistItIsAFile(const std::string& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        return failure("not a file: " + path);
    }
    return {};
}

tl::expected<std::string, ErrorPtr> readFile(const std::string& path) {
    auto isFile = insistItIsAFile(path);
    if (!isFile) return tl::make_unexpected(std::move(isFile).error());

    std::ifstream in(path, std::ios::binary);
    if (!in) return failure("cannot open file: " + path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) return failure("error reading file: " + path);
    return buffer.str();
}

std::string messageLine(std::string_view line) {
    // Where a tab lands, opened out.
    constexpr size_t kTabStop = 8;

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

}  // namespace amberedit::config::text
