#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "domain/ftn_address.hpp"
#include "domain/message.hpp"
#include "msgbase/binary_file.hpp"

/// How the format drivers write the values of an info report.
///
/// The three of them describe different records, but they describe them the
/// same way — a dword as hex and again in decimal, a stamp as the date it
/// stands for and again as the number stored — so the spelling lives here
/// rather than three times over. A report is read beside a specification and
/// beside a hexdump of the same bytes, which is why nearly everything in it is
/// shown twice: what is stored, and what it means.
namespace amberedit::msgbase::report {

/// How much of a stored block a report shows. A message is a few kilobytes at
/// most in practice, and the cap is for the ones that are not: the dump is a
/// diagnostic, and the head of a block is where anything worth diagnosing is.
/// A block cut short says so in its own title.
inline constexpr size_t kMaxDumpBytes = 4096;

/// A number as the base stores it: hex, uppercase, `digits` wide and closed
/// with the `h` every FTN tool has written since DOS.
[[nodiscard]] std::string hex(uint32_t value, int digits = 8);

/// The same with the decimal beside it — for the values that are a size, an
/// offset or a count, where both spellings are worth having.
[[nodiscard]] std::string hexAndDecimal(uint32_t value, int digits = 8);

/// An attribute word: the hex, and the bits behind it, so that an attribute can
/// be found in it against a specification.
[[nodiscard]] std::string bits(uint32_t value);

/// A decimal written to `digits` places with leading zeroes — how a JAM
/// subfield's number is printed, so that a column of them lines up.
[[nodiscard]] std::string padded(uint32_t value, int digits);

/// A stamp as the date it stands for, or "-" for one that is no date. The
/// number it is stored as belongs beside it and is the caller's, the formats
/// packing their stamps differently.
[[nodiscard]] std::string stamp(const domain::MessageDate& date);

/// An address, or an empty string where the message carries none — a field
/// that says nothing says it by being blank rather than by "0:0/0".
[[nodiscard]] std::string address(const domain::FtnAddress& value);

/// The heading over a hexdump: what the block holds and how long it is,
/// saying so where the cap above has left only the head of it on screen.
[[nodiscard]] std::string dumpTitle(std::string_view what, uint64_t stored, size_t shown);

/// Up to `kMaxDumpBytes` of a stored block, for a dump to be made of. A read
/// that fails comes back empty: a report is worth having without one of its
/// dumps, and there is nobody to tell either way.
[[nodiscard]] std::string readDump(const BinaryFile& file, uint64_t offset,
                                   uint64_t length);

/// One field of a report. The two spellings are one call each so that a driver
/// reads as the list of fields it is.
[[nodiscard]] domain::MessageInfoField field(std::string label, std::string value);

/// The same for a value that is text out of the message, in the message's own
/// charset — a name, a subject, a subfield the format keeps as data.
[[nodiscard]] domain::MessageInfoField textField(std::string label, std::string value);

}  // namespace amberedit::msgbase::report
