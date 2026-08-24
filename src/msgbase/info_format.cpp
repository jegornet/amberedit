#include "msgbase/info_format.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace amberedit::msgbase::report {

namespace {

constexpr char kDigits[] = "0123456789ABCDEF";

}  // namespace

std::string hex(uint32_t value, int digits) {
    std::string out(static_cast<size_t>(digits), '0');
    for (int i = digits - 1; i >= 0; --i) {
        out[static_cast<size_t>(i)] = kDigits[value & 0xfu];
        value >>= 4;
    }
    return out + "h";
}

std::string hexAndDecimal(uint32_t value, int digits) {
    return hex(value, digits) + " (" + std::to_string(value) + ")";
}

std::string bits(uint32_t value) {
    std::string out(32, '0');
    for (int i = 31; i >= 0; --i) {
        out[static_cast<size_t>(i)] = (value & 1u) != 0 ? '1' : '0';
        value >>= 1;
    }
    return out + "b";
}

std::string padded(uint32_t value, int digits) {
    std::string out = std::to_string(value);
    if (static_cast<int>(out.size()) >= digits) return out;
    return std::string(static_cast<size_t>(digits) - out.size(), '0') + out;
}

std::string stamp(const domain::MessageDate& date) {
    if (!date.isValid()) return "-";
    return date.format("%Y-%m-%d %H:%M:%S");
}

std::string address(const domain::FtnAddress& value) {
    return value.isValid() ? value.toString() : std::string{};
}

std::string dumpTitle(std::string_view what, uint64_t stored, size_t shown) {
    std::string title(what);
    if (shown < stored) {
        return title + ", first " + std::to_string(shown) + " of " +
               std::to_string(stored) + " bytes";
    }
    return title + ", " + std::to_string(stored) + " bytes";
}

std::string readDump(const BinaryFile& file, uint64_t offset, uint64_t length) {
    const auto wanted = static_cast<size_t>(std::min<uint64_t>(length, kMaxDumpBytes));
    if (wanted == 0) return {};
    std::string block(wanted, '\0');
    if (file.readAt(offset, &block[0], block.size()).failed()) return {};
    return block;
}

domain::MessageInfoField field(std::string label, std::string value) {
    return {std::move(label), std::move(value), /*text=*/false};
}

domain::MessageInfoField textField(std::string label, std::string value) {
    return {std::move(label), std::move(value), /*text=*/true};
}

}  // namespace amberedit::msgbase::report
