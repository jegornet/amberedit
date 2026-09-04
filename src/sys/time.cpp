#include "sys/time.hpp"

namespace amberedit::sys {

std::tm localTime(std::time_t when) {
    std::tm broken{};
#ifdef _WIN32
    localtime_s(&broken, &when);
#else
    localtime_r(&when, &broken);
#endif
    return broken;
}

std::tm utcTime(std::time_t when) {
    std::tm broken{};
#ifdef _WIN32
    gmtime_s(&broken, &when);
#else
    gmtime_r(&when, &broken);
#endif
    return broken;
}

int utcOffsetMinutes(std::time_t when) {
    const std::tm local = localTime(when);
    const std::tm utc = utcTime(when);

    int minutes = ((local.tm_hour - utc.tm_hour) * 60) + (local.tm_min - utc.tm_min);

    // The two can land on different days — Kamchatka in the morning is the
    // previous day in UTC — but never on days further apart than one, since no
    // zone is a whole day from Greenwich. Comparing the year first covers the
    // one case where the day numbers say the opposite of what happened: the
    // 31st of December against the 1st of January.
    int days = local.tm_yday - utc.tm_yday;
    if (local.tm_year != utc.tm_year) days = local.tm_year > utc.tm_year ? 1 : -1;
    minutes += days * 24 * 60;

    return minutes;
}

}  // namespace amberedit::sys
