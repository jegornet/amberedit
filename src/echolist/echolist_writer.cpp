#include "echolist/echolist_writer.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include "echolist/echolist_format.hpp"
#include "msgbase/byte_order.hpp"

namespace amberedit::echolist {
namespace {

using msgbase::bytes::writeU16;
using msgbase::bytes::writeU32;

void appendU16(std::string& out, uint16_t value) {
    unsigned char raw[2];
    writeU16(raw, value);
    out.append(reinterpret_cast<const char*>(raw), sizeof(raw));
}

void appendU32(std::string& out, uint32_t value) {
    unsigned char raw[4];
    writeU32(raw, value);
    out.append(reinterpret_cast<const char*>(raw), sizeof(raw));
}

void appendU64(std::string& out, uint64_t value) {
    appendU32(out, static_cast<uint32_t>(value & 0xffffffffu));
    appendU32(out, static_cast<uint32_t>(value >> 32));
}

/// A length and then the bytes. A path longer than the length can hold is cut
/// rather than refused: it is shown and compared, and nothing is addressed by
/// it — and no file system anywhere allows one.
void appendString(std::string& out, const std::string& value) {
    const std::string shown = value.size() > 0xffff ? value.substr(0, 0xffff) : value;
    appendU16(out, static_cast<uint16_t>(shown.size()));
    out += shown;
}

/// One entry on its way into the file, with the key the sort runs on beside it.
struct Pending {
    std::string key;
    const EchoEntry* entry{nullptr};
};

/// A tag or a description as the record writes it. Both lengths are written in
/// two bytes, and a truncated one would be read back as a record boundary in the
/// wrong place — so a field no echolist has is refused rather than cut.
Result<uint16_t> fieldLength(const std::string& field, const char* what,
                             const std::string& path) {
    if (field.size() > 0xffff) {
        return failure(path + ": an echolist line has a " + what + " of " +
                       std::to_string(field.size()) +
                       " bytes, which no echolist line has");
    }
    return static_cast<uint16_t>(field.size());
}

}  // namespace

Result<WriteReport> writeEcholistDb(const std::string& path,
                                    const std::vector<DbSource>& sources,
                                    std::time_t builtAt) {
    std::vector<Pending> pending;
    size_t total = 0;
    for (const auto& source : sources) total += source.entries.size();
    pending.reserve(total);

    for (const auto& source : sources) {
        for (const auto& entry : source.entries) {
            pending.push_back({foldTag(entry.tag), &entry});
        }
    }

    // Stable, so that entries left equal by the key stand in the order they were
    // handed over — which is the order of the config's `echolist` lines, and
    // within one of them the order of the file. The dedup below then keeps the
    // first, and "the first echolist named wins" is the whole of the rule.
    std::stable_sort(pending.begin(), pending.end(),
                     [](const Pending& a, const Pending& b) { return a.key < b.key; });

    WriteReport report;
    std::string records;
    std::string index;
    records.reserve(pending.size() * 64);
    index.reserve(pending.size() * format::kIndexEntrySize);

    const std::string* lastKey = nullptr;
    for (const auto& item : pending) {
        if (lastKey != nullptr && *lastKey == item.key) {
            ++report.duplicates;
            continue;
        }
        lastKey = &item.key;

        if (records.size() > 0xffffffffu) {
            return failure(path + ": the echolists do not fit in one file");
        }
        const auto tagLength = fieldLength(item.entry->tag, "tag", path);
        if (!tagLength) return tl::make_unexpected(tagLength.error());
        const auto descriptionLength =
            fieldLength(item.entry->description, "description", path);
        if (!descriptionLength) return tl::make_unexpected(descriptionLength.error());

        appendU32(index, static_cast<uint32_t>(records.size()));
        appendU16(records, *tagLength);
        appendU16(records, *descriptionLength);
        records += item.entry->tag;
        records += item.entry->description;
        ++report.areas;
    }

    // --- the table of the echolists this was made of -------------------------
    // The path the config wrote, the charset it stated, the file it named and
    // what that file was: the next start compares them against what the same
    // lines name then, and compiles again only where they differ.
    std::string sourceTable;
    for (const auto& source : sources) {
        appendString(sourceTable, source.state.spec);
        appendString(sourceTable, source.state.charset);
        appendString(sourceTable, source.state.path);
        appendU64(sourceTable, source.state.modified);
        appendU64(sourceTable, source.state.size);
    }

    // --- the header, once every part's size is known -------------------------
    const auto indexOffset = static_cast<uint32_t>(format::kHeaderSize);
    const auto sourceTableOffset = static_cast<uint32_t>(indexOffset + index.size());
    const auto recordsOffset =
        static_cast<uint32_t>(sourceTableOffset + sourceTable.size());
    const uint64_t fileSize =
        static_cast<uint64_t>(recordsOffset) + static_cast<uint64_t>(records.size());
    if (fileSize > 0xffffffffu) {
        return failure(path + ": the echolists do not fit in one file");
    }

    std::string header;
    header.append(format::kMagic, sizeof(format::kMagic));
    appendU16(header, format::kVersion);
    appendU16(header, static_cast<uint16_t>(format::kHeaderSize));
    appendU32(header, static_cast<uint32_t>(report.areas));
    appendU32(header, indexOffset);
    appendU32(header, recordsOffset);
    appendU32(header, static_cast<uint32_t>(records.size()));
    appendU32(header, static_cast<uint32_t>(sources.size()));
    appendU32(header, sourceTableOffset);
    appendU64(header, static_cast<uint64_t>(builtAt));
    appendU32(header, 0);
    header.resize(format::kHeaderSize, '\0');

    // Written beside the destination and renamed over it: a reader either sees
    // the whole of the file it had or the whole of this one.
    namespace fs = std::filesystem;
    const fs::path destination(path);
    const fs::path temporary =
        destination.parent_path() / (destination.filename().string() + ".new");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return failure("cannot write the compiled echolist: " + temporary.string());
        }
        out.write(header.data(), static_cast<std::streamsize>(header.size()));
        out.write(index.data(), static_cast<std::streamsize>(index.size()));
        out.write(sourceTable.data(), static_cast<std::streamsize>(sourceTable.size()));
        out.write(records.data(), static_cast<std::streamsize>(records.size()));
        out.close();
        if (!out) {
            std::error_code ec;
            fs::remove(temporary, ec);
            return failure("cannot write the compiled echolist: " + temporary.string());
        }
    }

    std::error_code ec;
    fs::rename(temporary, destination, ec);
    if (ec) {
        fs::remove(temporary, ec);
        return failure("cannot put the compiled echolist at " + path + ": " +
                       ec.message());
    }

    report.bytes = static_cast<size_t>(fileSize);
    return report;
}

}  // namespace amberedit::echolist
