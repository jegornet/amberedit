#include "nodelist/nodelist_writer.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

#include "msgbase/byte_order.hpp"
#include "nodelist/nodelist_format.hpp"

namespace amberedit::nodelist {
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

/// One entry on its way into the file, with everything the sorts need beside it.
struct Pending {
    uint64_t key{0};
    const NodeEntry* entry{nullptr};
    uint8_t source{0};
};

/// One position of one folded name: where the text starts in the pool, and the
/// node it names.
struct NameEntry {
    uint32_t poolOffset{0};
    uint32_t node{0};
};

/// The length of a field as the record writes it. A nodelist line is under
/// 200 bytes and every field of it far shorter, so this only fires on a file
/// that is not a nodelist at all — but the length is written in two bytes, and
/// a truncated one would be read back as a record boundary in the wrong place.
uint16_t fieldLength(const std::string& field, const char* what,
                     const std::string& path) {
    if (field.size() > 0xffff) {
        throw std::runtime_error(path + ": a nodelist line has a " + what + " field of " +
                                 std::to_string(field.size()) +
                                 " bytes, which no nodelist line has");
    }
    return static_cast<uint16_t>(field.size());
}

}  // namespace

WriteReport writeNodelistDb(const std::string& path, const std::vector<DbSource>& sources,
                            std::time_t builtAt) {
    if (sources.size() > format::kMaxSources) {
        throw std::runtime_error(path + ": " + std::to_string(sources.size()) +
                                 " nodelists is more than the " +
                                 std::to_string(format::kMaxSources) +
                                 " one compiled file can be made of");
    }

    std::vector<Pending> pending;
    size_t total = 0;
    for (const auto& source : sources) total += source.entries.size();
    pending.reserve(total);

    for (size_t s = 0; s < sources.size(); ++s) {
        for (const auto& entry : sources[s].entries) {
            pending.push_back(
                {format::addressKey(entry.address.zone, entry.address.net,
                                    entry.address.node, entry.address.point),
                 &entry, static_cast<uint8_t>(s)});
        }
    }

    // Stable, so that entries left equal by the key stand in the order they
    // were handed over — which is the order of the config's `nodelist` lines,
    // and within one of them the order of the file. The dedup below then keeps
    // the first, and "the first nodelist named wins" is the whole of the rule.
    std::stable_sort(pending.begin(), pending.end(),
                     [](const Pending& a, const Pending& b) { return a.key < b.key; });

    WriteReport report;
    std::vector<Pending> kept;
    kept.reserve(pending.size());
    for (auto& item : pending) {
        if (!kept.empty() && kept.back().key == item.key) {
            ++report.duplicates;
            continue;
        }
        kept.push_back(item);
    }

    // --- the records, and the address index over them ------------------------
    std::string records;
    std::string addressIndex;
    records.reserve(kept.size() * 96);
    addressIndex.reserve(kept.size() * format::kAddressEntrySize);

    for (const auto& item : kept) {
        const NodeEntry& entry = *item.entry;
        if (records.size() > 0xffffffffu) {
            throw std::runtime_error(path + ": the nodelists do not fit in one file");
        }
        appendU64(addressIndex, item.key);
        appendU32(addressIndex, static_cast<uint32_t>(records.size()));

        records += static_cast<char>(static_cast<uint8_t>(entry.keyword));
        records += static_cast<char>(item.source);
        appendU16(records, entry.address.zone);
        appendU16(records, entry.address.net);
        appendU16(records, entry.address.node);
        appendU16(records, entry.address.point);
        appendU32(records, entry.speed);
        appendU16(records, fieldLength(entry.system, "system name", path));
        appendU16(records, fieldLength(entry.location, "location", path));
        appendU16(records, fieldLength(entry.sysop, "sysop", path));
        appendU16(records, fieldLength(entry.phone, "phone", path));
        appendU16(records, fieldLength(entry.flags, "flags", path));
        records += entry.system;
        records += entry.location;
        records += entry.sysop;
        records += entry.phone;
        records += entry.flags;

        if (entry.address.point != 0) {
            ++report.points;
        } else {
            ++report.nodes;
        }
    }

    // --- the name pool, and the suffix array over it -------------------------
    std::string namePool;
    std::unordered_map<std::string, uint32_t> poolOffsets;
    std::vector<NameEntry> nameIndex;
    namePool.reserve(kept.size() * 16);
    nameIndex.reserve(kept.size() * 16);

    for (size_t i = 0; i < kept.size(); ++i) {
        const std::string folded = foldName(kept[i].entry->sysop);
        if (folded.empty()) continue;

        auto found = poolOffsets.find(folded);
        if (found == poolOffsets.end()) {
            if (namePool.size() > 0xffffffffu - folded.size() - 1) {
                throw std::runtime_error(path +
                                         ": the sysop names do not fit in one file");
            }
            found =
                poolOffsets.emplace(folded, static_cast<uint32_t>(namePool.size())).first;
            namePool += folded;
            namePool += '\0';
        }

        // Every position of the name that is part of a word, so that a search
        // for any run of characters inside it is a run of this array — the
        // whole name, a surname, or three letters out of the middle of one.
        // Positions on a blank are left out: the text a search is folded to
        // never begins with one, so an entry for one could never be found.
        const uint32_t base = found->second;
        for (size_t at = 0; at < folded.size(); ++at) {
            if (folded[at] == ' ') continue;
            nameIndex.push_back(
                {base + static_cast<uint32_t>(at), static_cast<uint32_t>(i)});
        }
    }

    const char* pool = namePool.c_str();
    std::sort(nameIndex.begin(), nameIndex.end(),
              [pool](const NameEntry& a, const NameEntry& b) {
                  const int order = std::strcmp(pool + a.poolOffset, pool + b.poolOffset);
                  if (order != 0) return order < 0;
                  // The node breaks the tie so that the run a search finds is
                  // in address order and the reader has nothing left to sort.
                  return a.node < b.node;
              });

    std::string nameIndexBytes;
    nameIndexBytes.reserve(nameIndex.size() * format::kNameEntrySize);
    for (const auto& item : nameIndex) {
        appendU32(nameIndexBytes, item.poolOffset);
        appendU32(nameIndexBytes, item.node);
    }

    // --- the table of the nodelists this was made of -------------------------
    // The line the config wrote, the file it named, and what that file was: the
    // first is what a node says it came from, and the other three are what the
    // next start compares against to find out whether anything has changed.
    std::string sourceTable;
    for (const auto& source : sources) {
        appendString(sourceTable, source.state.spec);
        appendString(sourceTable, source.state.path);
        appendU64(sourceTable, source.state.modified);
        appendU64(sourceTable, source.state.size);
    }

    // --- the header, once every part's size is known -------------------------
    const auto addressIndexOffset = static_cast<uint32_t>(format::kHeaderSize);
    const auto nameIndexOffset =
        static_cast<uint32_t>(addressIndexOffset + addressIndex.size());
    const auto namePoolOffset =
        static_cast<uint32_t>(nameIndexOffset + nameIndexBytes.size());
    const auto sourceTableOffset =
        static_cast<uint32_t>(namePoolOffset + namePool.size());
    const auto recordsOffset =
        static_cast<uint32_t>(sourceTableOffset + sourceTable.size());
    const uint64_t fileSize =
        static_cast<uint64_t>(recordsOffset) + static_cast<uint64_t>(records.size());
    if (fileSize > 0xffffffffu) {
        throw std::runtime_error(path + ": the nodelists do not fit in one file");
    }

    std::string header;
    header.append(format::kMagic, sizeof(format::kMagic));
    appendU16(header, format::kVersion);
    appendU16(header, static_cast<uint16_t>(format::kHeaderSize));
    appendU32(header, static_cast<uint32_t>(kept.size()));
    appendU32(header, recordsOffset);
    appendU32(header, static_cast<uint32_t>(records.size()));
    appendU32(header, addressIndexOffset);
    appendU32(header, static_cast<uint32_t>(nameIndex.size()));
    appendU32(header, nameIndexOffset);
    appendU32(header, namePoolOffset);
    appendU32(header, static_cast<uint32_t>(namePool.size()));
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
            throw std::runtime_error("cannot write the compiled nodelist: " +
                                     temporary.string());
        }
        out.write(header.data(), static_cast<std::streamsize>(header.size()));
        out.write(addressIndex.data(), static_cast<std::streamsize>(addressIndex.size()));
        out.write(nameIndexBytes.data(),
                  static_cast<std::streamsize>(nameIndexBytes.size()));
        out.write(namePool.data(), static_cast<std::streamsize>(namePool.size()));
        out.write(sourceTable.data(), static_cast<std::streamsize>(sourceTable.size()));
        out.write(records.data(), static_cast<std::streamsize>(records.size()));
        out.close();
        if (!out) {
            std::error_code ec;
            fs::remove(temporary, ec);
            throw std::runtime_error("cannot write the compiled nodelist: " +
                                     temporary.string());
        }
    }

    std::error_code ec;
    fs::rename(temporary, destination, ec);
    if (ec) {
        fs::remove(temporary, ec);
        throw std::runtime_error("cannot put the compiled nodelist at " + path + ": " +
                                 ec.message());
    }

    report.bytes = static_cast<size_t>(fileSize);
    return report;
}

}  // namespace amberedit::nodelist
