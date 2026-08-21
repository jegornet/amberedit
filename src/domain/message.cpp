#include "domain/message.hpp"

#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amberedit::domain {

namespace {

/// string_view::starts_with is C++20 and the project is C++17. Spelled out here
/// rather than taken from config/text_util so that domain/ keeps including
/// nothing but domain/.
bool startsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

/// ASCII case folding and nothing else, for the same reason config/text_util
/// spells its own out: a locale a user may well have set — KOI8-R, CP1251 —
/// folds the high half of the byte range too, and an attribute is written in
/// ASCII wherever it is written.
bool iequals(std::string_view a, std::string_view b) {
    const auto fold = [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (fold(a[i]) != fold(b[i])) return false;
    }
    return true;
}

/// One attribute and the short form FTN readers show it in.
struct AttributeName {
    uint32_t bit;
    const char* name;
};

/// Every attribute AmberEdit has a bit for, in the order a message states them:
/// what became of it first, then whether it is private, then the rest.
///
/// One table read both ways — messageAttributes() writes these names and
/// messageAttributeBit() reads them back — so the word a config is written with
/// is by construction the word the screens show, and neither can drift from the
/// other.
const std::vector<AttributeName>& attributeNames() {
    static const std::vector<AttributeName> names{
        {attr::kRead, "Rcv"},          {attr::kSent, "Snt"},
        {attr::kPrivate, "Pvt"},       {attr::kCrash, "Cra"},
        {attr::kHold, "Hld"},          {attr::kDirect, "Dir"},
        {attr::kImmediate, "Imm"},     {attr::kFile, "Att"},
        {attr::kFileRequest, "Frq"},   {attr::kInTransit, "Trs"},
        {attr::kOrphan, "Orp"},        {attr::kKillSent, "K/s"},
        {attr::kLocal, "Loc"},         {attr::kReceiptRequest, "Rrq"},
        {attr::kIsReceipt, "Cpt"},     {attr::kAuditRequest, "Arq"},
        {attr::kUpdateRequest, "Urq"}, {attr::kScanned, "Scn"},
    };
    return names;
}

}  // namespace

bool isTearline(std::string_view line) {
    return line == "---" || startsWith(line, "--- ");
}

bool isOriginLine(std::string_view line) {
    return startsWith(line, " * Origin:");
}

void markTrailer(std::vector<MessageLine>& lines) {
    auto index = static_cast<int>(lines.size()) - 1;
    const auto at = [&lines](int i) -> MessageLine& {
        return lines[static_cast<size_t>(i)];
    };
    const auto skipToVisible = [&] {
        while (index >= 0 && (at(index).kludge || at(index).text.find_first_not_of(' ') ==
                                                      std::string::npos)) {
            --index;
        }
    };

    skipToVisible();
    if (index >= 0 && isOriginLine(at(index).text)) {
        at(index).trailer = true;
        --index;
        skipToVisible();
    }
    // A message may carry a tearline with no origin — netmail and local areas
    // routinely do — so this is checked whether or not an origin was found.
    if (index >= 0 && isTearline(at(index).text)) at(index).trailer = true;
}

std::string MessageBody::text() const {
    std::string out;
    for (const auto& line : lines) {
        if (line.kludge) continue;
        if (!out.empty()) out += '\n';
        out += line.text;
    }
    return out;
}

std::vector<std::string> MessageBody::kludges() const {
    std::vector<std::string> out;
    for (const auto& line : lines) {
        if (line.kludge) out.push_back(line.text);
    }
    return out;
}

namespace {

/// Days from 1970-01-01 to a civil date, by the usual era-based algorithm.
/// It is what `%a` and `%j` need: an FTN stamp holds no weekday, and mktime()
/// would work one out in the local time zone — which the stamp is not in.
int64_t daysFromCivil(int year, int month, int day) {
    year -= month <= 2 ? 1 : 0;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const int64_t yoe = year - era * 400;  // [0, 399]
    const int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;  // [0, 146096]
    return era * 146097 + doe - 719468;
}

/// The stamp as strftime wants it. The fields are clamped on the way in: a
/// base can hold anything, and strftime indexes its month and weekday tables
/// by them, so a corrupt stamp would otherwise read past their ends. A month
/// shown wrong is a better answer than a crash.
std::tm brokenDown(const MessageDate& date) {
    const auto clamp = [](unsigned value, unsigned low, unsigned high) {
        return static_cast<int>(value < low ? low : (value > high ? high : value));
    };

    std::tm when{};
    when.tm_year = static_cast<int>(date.year) - 1900;
    when.tm_mon = clamp(date.month, 1, 12) - 1;
    when.tm_mday = clamp(date.day, 1, 31);
    when.tm_hour = clamp(date.hour, 0, 23);
    when.tm_min = clamp(date.minute, 0, 59);
    when.tm_sec = clamp(date.second, 0, 59);
    when.tm_isdst = -1;  // the stamp is in no time zone, so neither is this

    const int year = when.tm_year + 1900;
    const int64_t days = daysFromCivil(year, when.tm_mon + 1, when.tm_mday);
    // 1970-01-01 was a Thursday, and the modulo is brought back into range for
    // the dates before it that a base is perfectly capable of holding.
    when.tm_wday = static_cast<int>(((days + 4) % 7 + 7) % 7);
    when.tm_yday = static_cast<int>(days - daysFromCivil(year, 1, 1));
    return when;
}

/// The format with every `%z` in it replaced by the zone the caller passed,
/// before strftime ever sees one.
///
/// strftime answers `%z` out of `struct tm`, which for an FTN stamp holds no
/// zone at all: it would write "+0000" and have the message say it was written
/// in UTC. The offset a message states is its TZUTC's, and that is what goes in
/// here — or nothing, where it states none.
///
/// `%%z` is left alone: it is a literal '%' followed by a 'z' and was never a
/// specifier. Every other specifier is copied over untouched, whatever it is —
/// what it means is the C library's business.
///
/// A '%' at the very end is doubled, and that is not tidiness. It has nothing
/// after it to make a specifier of, and what strftime does with one is left
/// undefined: glibc writes the character, other libraries call the whole format
/// an error and write nothing, losing the entire stamp over one stray character
/// at the end. `%%` is a literal '%' in every library, so
/// doubling it is what makes the answer the same one everywhere. The format is
/// the user's `reader_datetime_format`, so a stray '%' is a thing that happens.
std::string withZone(const std::string& spec, std::string_view zone) {
    std::string out;
    out.reserve(spec.size() + zone.size());
    for (size_t at = 0; at < spec.size(); ++at) {
        if (spec[at] != '%') {
            out += spec[at];
            continue;
        }
        if (at + 1 == spec.size()) {
            out += "%%";
            continue;
        }
        if (spec[at + 1] == 'z') {
            // strftime reads what goes in here after this, so a '%' in the
            // zone has to stop being one.
            for (const char c : zone) {
                out += c;
                if (c == '%') out += '%';
            }
        } else {
            out += spec[at];
            out += spec[at + 1];
        }
        ++at;
    }
    return out;
}

/// The stamp with the blank at either end taken off.
///
/// A specifier that writes nothing leaves the space beside it behind, and the
/// one that does that is `%z`, which a message stating no zone answers with
/// nothing at all: "%d %b %y %H:%M %z" would otherwise write a stamp ending in
/// a space for every such message, and a column measured off it would be a
/// column wider than what is in it. Trimmed rather than special-cased, because
/// the same is true of a format written with a space at either end.
std::string trimmed(std::string stamp) {
    const auto blank = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
    };
    size_t begin = 0;
    while (begin < stamp.size() && blank(stamp[begin])) ++begin;
    size_t end = stamp.size();
    while (end > begin && blank(stamp[end - 1])) --end;
    return stamp.substr(begin, end - begin);
}

}  // namespace

std::string MessageDate::format(const std::string& spec, std::string_view zone) const {
    if (!isValid()) return "";
    const std::tm when = brokenDown(*this);

    // strftime answers 0 both for a format that did not fit and for one that
    // legitimately writes nothing, so it is asked with one character in front
    // that is then dropped: a zero can then only mean the buffer.
    const std::string probe = "\x01" + withZone(spec, zone);
    std::vector<char> buffer(256);
    while (true) {
        const size_t written =
            std::strftime(buffer.data(), buffer.size(), probe.c_str(), &when);
        if (written > 0) return trimmed(std::string(buffer.data() + 1, written - 1));
        if (buffer.size() >= 8192) return "";
        buffer.resize(buffer.size() * 4);
    }
}

bool isUnsent(uint32_t attributes) {
    return (attributes & attr::kLocal) != 0 && (attributes & attr::kSent) == 0;
}

bool isUnsent(const MessageHeader& header) { return isUnsent(header.attributes); }

std::vector<std::string> messageAttributes(uint32_t attributes) {
    std::vector<std::string> names;
    for (const AttributeName& entry : attributeNames()) {
        // The virtual attribute stands where a bit of its own would have: Loc
        // set and Snt clear, and the first thing worth knowing about a message
        // of one's own, so it is stated rather than left to be inferred from a
        // missing Snt.
        if (entry.bit == attr::kSent && isUnsent(attributes)) names.emplace_back("Uns");
        if ((attributes & entry.bit) != 0) names.emplace_back(entry.name);
    }
    return names;
}

std::optional<uint32_t> messageAttributeBit(std::string_view name) {
    for (const AttributeName& entry : attributeNames()) {
        if (iequals(name, entry.name)) return entry.bit;
    }
    return std::nullopt;
}

std::vector<std::string> messageAttributeNames() {
    std::vector<std::string> names;
    names.reserve(attributeNames().size());
    for (const AttributeName& entry : attributeNames()) names.emplace_back(entry.name);
    return names;
}

std::vector<std::string> messageAttributes(const MessageHeader& header) {
    return messageAttributes(header.attributes);
}

}  // namespace amberedit::domain
