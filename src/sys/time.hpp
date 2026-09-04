#pragma once

#include <ctime>

namespace amberedit::sys {

/// The local calendar, and how far it stands from UTC.
///
/// Two things here are not `<ctime>`'s to give. The reentrant `localtime_r` is
/// POSIX and Windows spells it `localtime_s` with the arguments the other way
/// round; and `tm_gmtoff`, which is where the zone in a TZUTC kludge came from,
/// is a BSD and glibc extension that ANSI's `struct tm` does not have at all.
///
/// The offset is therefore not read off the field but worked out, by asking for
/// the same instant twice and subtracting. That is portable by construction —
/// it needs nothing of `struct tm` beyond what the standard guarantees — and it
/// answers for the moment in question rather than for today, so a message
/// written either side of a daylight-saving change carries the offset that was
/// actually in force when it was written.

/// The local time at `when`, broken down. Zeroed rather than left indeterminate
/// if the platform refuses the conversion.
[[nodiscard]] std::tm localTime(std::time_t when);

/// UTC at `when`, broken down. Same contract as `localTime`.
[[nodiscard]] std::tm utcTime(std::time_t when);

/// How far local time stands ahead of UTC at `when`, in minutes: 180 for MSK,
/// -300 for New York in winter. This is what FTS-4008's TZUTC is written from.
[[nodiscard]] int utcOffsetMinutes(std::time_t when);

}  // namespace amberedit::sys
