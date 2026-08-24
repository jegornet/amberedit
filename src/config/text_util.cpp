#include "config/text_util.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace amberedit::config::text {

Result<void> insistItIsAFile(const std::string& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        return failure("not a file: " + path);
    }
    return {};
}

Result<std::string> readFile(const std::string& path) {
    auto isFile = insistItIsAFile(path);
    if (!isFile) return tl::make_unexpected(std::move(isFile).error());

    std::ifstream in(path, std::ios::binary);
    if (!in) return failure("cannot open file: " + path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) return failure("error reading file: " + path);
    return buffer.str();
}

}  // namespace amberedit::config::text
