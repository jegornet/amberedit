#include "config/text_util.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace amberedit::config::text {

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open file: " + path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) throw std::runtime_error("error reading file: " + path);
    return buffer.str();
}

}  // namespace amberedit::config::text
