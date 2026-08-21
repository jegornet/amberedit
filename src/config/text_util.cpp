#include "config/text_util.hpp"

#include <fstream>
#include <sstream>

namespace amberedit::config::text {

Result<std::string> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return failure("cannot open file: " + path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) return failure("error reading file: " + path);
    return buffer.str();
}

}  // namespace amberedit::config::text
