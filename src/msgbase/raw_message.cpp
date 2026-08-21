#include "msgbase/raw_message.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "config/text_util.hpp"

namespace amberedit::msgbase {

namespace {

constexpr char kSoh = '\x01';

constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

/// Days from 1970-01-01 to the given civil date, by Howard Hinnant's
/// `days_from_civil`. Worked out rather than left to `mktime()`, which answers
/// in the local zone — and a JAM stamp is in the zone of whoever wrote the
/// message, which is not this machine's and not knowable from here.
int64_t daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2 ? 1 : 0;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const auto yoe = static_cast<unsigned>(year - (era * 400));
    const unsigned doy = ((153u * (month + (month > 2 ? -3 : 9)) + 2u) / 5u) + day - 1;
    const unsigned doe = (yoe * 365) + (yoe / 4) - (yoe / 100) + doy;
    return (era * 146097) + static_cast<int64_t>(doe) - 719468;
}

/// The inverse, `civil_from_days`.
void civilFromDays(int64_t days, int* year, unsigned* month, unsigned* day) {
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const auto doe = static_cast<uint64_t>(days - (era * 146097));
    const uint64_t yoe = (doe - (doe / 1460) + (doe / 36524) - (doe / 146096)) / 365;
    const int64_t y = static_cast<int64_t>(yoe) + (era * 400);
    const uint64_t doy = doe - ((365 * yoe) + (yoe / 4) - (yoe / 100));
    const uint64_t mp = ((5 * doy) + 2) / 153;
    *day = static_cast<unsigned>(doy - (((153 * mp) + 2) / 5) + 1);
    *month = static_cast<unsigned>(mp + (mp < 10 ? 3 : -9));
    *year = static_cast<int>(y + (*month <= 2 ? 1 : 0));
}

/// What a TZUTC control line says, as an offset — or nothing, where what it
/// says is not one. FTS-4008 asks for `[-]hhmm` with all four digits present
/// and no plus in front, and asks readers to accept a plus anyway; the sign is
/// put back on the way out, since that is how an offset is shown.
std::string offsetFrom(std::string_view value) {
    value = config::text::trim(value);
    bool west = false;
    if (!value.empty() && (value.front() == '-' || value.front() == '+')) {
        west = value.front() == '-';
        value.remove_prefix(1);
    }

    // Four digits and nothing else. A message is perfectly capable of saying
    // "TZUTC: MSK", and half of that in a Date column would be worse than the
    // blank a message stating no zone at all gets.
    const auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    if (value.size() != 4 || !std::all_of(value.begin(), value.end(), isDigit)) return {};
    if (value == "0000") west = false;  // UTC is not west of itself
    return (west ? "-" : "+") + std::string(value);
}

/// What the first `^Aname` control line carries, empty where there is none.
/// The name is matched as it is written, colon and all where the kludge has
/// one — INTL, FMPT and TOPT have none.
std::string kludgeValue(std::string_view control, std::string_view name) {
    size_t pos = 0;
    while (pos < control.size()) {
        size_t lineEnd = control.find_first_of("\r\n", pos);
        if (lineEnd == std::string_view::npos) lineEnd = control.size();
        std::string_view line = control.substr(pos, lineEnd - pos);
        pos = lineEnd + 1;
        if (!line.empty() && line.front() == kSoh) line.remove_prefix(1);
        if (!config::text::startsWith(line, name)) continue;
        line.remove_prefix(name.size());
        while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
        return std::string(line);
    }
    return {};
}

/// The point number an FMPT or TOPT line states — the whole of what it
/// carries — and zero where what it carries is not one.
uint16_t pointFrom(std::string_view value) {
    value = config::text::trim(value);
    if (value.empty() || value.size() > 5) return 0;
    uint32_t point = 0;
    for (const char ch : value) {
        if (ch < '0' || ch > '9') return 0;
        point = (point * 10) + static_cast<uint32_t>(ch - '0');
    }
    return point <= 0xffff ? static_cast<uint16_t>(point) : 0;
}

}  // namespace

std::string controlBlockToKludges(std::string_view block) {
    // The block is a C string as far as its writer is concerned: Squish stores
    // the trailing NUL in it and counts it in the length.
    const size_t end = block.find('\0');
    if (end != std::string_view::npos) block = block.substr(0, end);

    std::string out;
    size_t pos = 0;
    while (pos < block.size() && block[pos] == kSoh) {
        size_t lineEnd = block.find(kSoh, pos + 1);
        if (lineEnd == std::string_view::npos) lineEnd = block.size();
        const std::string_view line = block.substr(pos + 1, lineEnd - pos - 1);
        if (line.empty()) break;  // a stray ^A at the end is not a kludge

        // AREA: never carried a ^A in the message it came from; it is here
        // because it stands where the kludges do.
        if (!config::text::startsWith(line, "AREA:")) out += kSoh;
        out.append(line);
        out += '\r';
        pos = lineEnd;
    }
    return out;
}

std::string kludgesToControlBlock(const std::vector<std::string>& kludges) {
    std::string out;
    for (const auto& kludge : kludges) {
        if (kludge.empty()) continue;
        out += kSoh;
        out += kludge;
    }
    return out;
}

std::string tzutcOffsetOf(std::string_view control) {
    size_t pos = 0;
    while (pos < control.size()) {
        size_t lineEnd = control.find_first_of("\r\n", pos);
        if (lineEnd == std::string_view::npos) lineEnd = control.size();
        std::string_view line = control.substr(pos, lineEnd - pos);
        pos = lineEnd + 1;
        if (!line.empty() && line.front() == kSoh) line.remove_prefix(1);

        // The longer name first: the shorter one is a prefix of it.
        std::string_view value;
        if (config::text::startsWith(line, "TZUTCINFO:")) {
            value = line.substr(std::strlen("TZUTCINFO:"));
        } else if (config::text::startsWith(line, "TZUTC:")) {
            value = line.substr(std::strlen("TZUTC:"));
        } else {
            continue;
        }

        // A line naming it and saying nothing usable is stepped over rather
        // than answered with: a message carrying both names has the other one
        // left to say it.
        const std::string offset = offsetFrom(value);
        if (!offset.empty()) return offset;
    }
    return {};
}

void completeAddresses(RawHeader& header, std::string_view control) {
    // INTL is read only where a zone is missing, and read as a pair: FSC-0004
    // writes the destination first and the origin second.
    if (header.origAddr.zone == 0 || header.destAddr.zone == 0) {
        const std::string intl = kludgeValue(control, "INTL");
        const size_t space = intl.find(' ');
        if (space != std::string::npos) {
            const auto dest = domain::FtnAddress::parse(intl.substr(0, space));
            const auto orig = domain::FtnAddress::parse(intl.substr(space + 1));
            // An INTL naming other nodes than the header does belongs to a
            // message this one was routed inside of, and says nothing about
            // this one's zones.
            if (dest && orig && dest->net == header.destAddr.net &&
                dest->node == header.destAddr.node &&
                orig->net == header.origAddr.net &&
                orig->node == header.origAddr.node) {
                if (header.origAddr.zone == 0) header.origAddr.zone = orig->zone;
                if (header.destAddr.zone == 0) header.destAddr.zone = dest->zone;
            }
        }
    }
    if (header.origAddr.point == 0) {
        header.origAddr.point = pointFrom(kludgeValue(control, "FMPT"));
    }
    if (header.destAddr.point == 0) {
        header.destAddr.point = pointFrom(kludgeValue(control, "TOPT"));
    }
}

void splitLeadingKludges(std::string_view body, std::string* control, std::string* text) {
    std::string kludges;
    size_t pos = 0;
    while (pos < body.size()) {
        const std::string_view rest = body.substr(pos);
        const bool isArea = config::text::startsWith(rest, "AREA:");
        if (rest.front() != kSoh && !isArea) break;

        size_t lineEnd = body.find_first_of("\r\n", pos);
        if (lineEnd == std::string_view::npos) lineEnd = body.size();
        std::string_view line = body.substr(pos, lineEnd - pos);
        if (!isArea) line.remove_prefix(1);  // the ^A is put back on the way out

        if (!isArea) kludges += kSoh;
        kludges.append(line);
        kludges += '\r';

        // Exactly one line break is stepped over, so that a blank line after
        // the kludges stays in the text where its writer put it — and ends the
        // control block, the kludges being the first lines of the message and
        // not lines scattered through it.
        pos = lineEnd;
        if (pos < body.size() && body[pos] == '\r') ++pos;
        if (pos < body.size() && body[pos] == '\n') ++pos;
    }

    if (control != nullptr) *control = std::move(kludges);
    if (text != nullptr) *text = std::string(body.substr(pos));
}

domain::MessageDate fromDosStamp(uint16_t date, uint16_t time) {
    domain::MessageDate out;
    if (date == 0) return out;
    const unsigned day = date & 31u;
    const unsigned month = (date >> 5) & 15u;
    if (day == 0 || month == 0) return out;
    out.year = static_cast<uint16_t>(1980 + ((date >> 9) & 127u));
    out.month = static_cast<uint8_t>(month);
    out.day = static_cast<uint8_t>(day);
    out.hour = static_cast<uint8_t>((time >> 11) & 31u);
    out.minute = static_cast<uint8_t>((time >> 5) & 63u);
    out.second = static_cast<uint8_t>((time & 31u) * 2);  // stored in two-second units
    return out;
}

void toDosStamp(const domain::MessageDate& date, uint16_t* outDate, uint16_t* outTime) {
    if (!date.isValid()) {
        *outDate = 0;
        *outTime = 0;
        return;
    }
    const int year = static_cast<int>(date.year) - 1980;
    *outDate = static_cast<uint16_t>((date.day & 31u) | ((date.month & 15u) << 5) |
                                     ((year > 0 ? year : 0) & 127) << 9);
    *outTime =
        static_cast<uint16_t>(((date.second / 2) & 31u) | ((date.minute & 63u) << 5) |
                              ((date.hour & 31u) << 11));
}

domain::MessageDate fromUnixStamp(uint32_t seconds) {
    domain::MessageDate out;
    if (seconds == 0) return out;
    const auto days = static_cast<int64_t>(seconds / 86400);
    const auto rest = static_cast<uint32_t>(seconds % 86400);
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civilFromDays(days, &year, &month, &day);
    out.year = static_cast<uint16_t>(year);
    out.month = static_cast<uint8_t>(month);
    out.day = static_cast<uint8_t>(day);
    out.hour = static_cast<uint8_t>(rest / 3600);
    out.minute = static_cast<uint8_t>((rest / 60) % 60);
    out.second = static_cast<uint8_t>(rest % 60);
    return out;
}

uint32_t toUnixStamp(const domain::MessageDate& date) {
    if (!date.isValid()) return 0;
    const int64_t days = daysFromCivil(date.year, date.month, date.day);
    const int64_t seconds = (days * 86400) + (int64_t{date.hour} * 3600) +
                            (int64_t{date.minute} * 60) + date.second;
    return seconds > 0 ? static_cast<uint32_t>(seconds) : 0;
}

std::string ftscDate(const domain::MessageDate& date) {
    if (!date.isValid()) return {};
    char buffer[24];
    const unsigned month = date.month >= 1 && date.month <= 12 ? date.month : 1;
    std::snprintf(buffer, sizeof(buffer), "%02d %s %02d  %02d:%02d:%02d", date.day,
                  kMonths[month - 1], date.year % 100, date.hour, date.minute,
                  date.second);
    return buffer;
}

domain::MessageDate parseFtscDate(std::string_view text) {
    domain::MessageDate out;
    // "01 Jan 86  02:34:56", and the same with a single-digit day or one
    // leading space too many — both are met in the wild.
    unsigned day = 0;
    char month[4] = {0};
    unsigned year = 0;
    unsigned hour = 0;
    unsigned minute = 0;
    unsigned second = 0;
    const std::string copy(text);
    if (std::sscanf(copy.c_str(), "%u %3s %u %u:%u:%u", &day, month, &year, &hour,
                    &minute, &second) != 6) {
        return out;
    }

    const auto* found = std::find_if(
        std::begin(kMonths), std::end(kMonths),
        [&month](const char* name) { return config::text::iequals(name, month); });
    if (found == std::end(kMonths) || day == 0 || day > 31) return out;

    out.day = static_cast<uint8_t>(day);
    out.month = static_cast<uint8_t>(found - std::begin(kMonths) + 1);
    // Two digits, and the base is older than the century: 80 and over is the
    // twentieth, the rest the twenty-first. Four digits are taken as given.
    out.year = static_cast<uint16_t>(year >= 100  ? year
                                     : year >= 80 ? 1900 + year
                                                  : 2000 + year);
    out.hour = static_cast<uint8_t>(hour % 24);
    out.minute = static_cast<uint8_t>(minute % 60);
    out.second = static_cast<uint8_t>(second % 60);
    return out;
}

std::string fromFixedField(const unsigned char* field, size_t capacity) {
    size_t length = 0;
    while (length < capacity && field[length] != '\0') ++length;
    return std::string(reinterpret_cast<const char*>(field), length);
}

void toFixedField(unsigned char* field, size_t capacity, std::string_view text) {
    const size_t length = std::min(text.size(), capacity - 1);
    std::memset(field, 0, capacity);
    if (length != 0) std::memcpy(field, text.data(), length);
}

}  // namespace amberedit::msgbase
