#include "nodelist/nodelist_db.hpp"
#include "config/text_util.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "msgbase/byte_order.hpp"

namespace amberedit::nodelist {
namespace {

using msgbase::bytes::readU16;
using msgbase::bytes::readU32;

[[nodiscard]] uint64_t readU64(const unsigned char* bytes) {
    return static_cast<uint64_t>(readU32(bytes)) |
           static_cast<uint64_t>(readU32(bytes + 4)) << 32;
}

/// One match on its way into the relevance order.
struct Ranked {
    int rank{0};
    size_t length{0};
    size_t node{0};
};

/// How closely `query` matches `name`, both folded — lower is closer. The whole
/// name first, then a name the query begins, then a word inside it, and last
/// the middle of a word.
[[nodiscard]] int rankOf(const std::string& name, const std::string& query) {
    if (name == query) return 0;
    const size_t at = name.find(query);
    if (at == 0) return 1;
    if (at != std::string::npos && at > 0 && name[at - 1] == ' ') return 2;
    return 3;
}

/// Whether a run of `count` bytes at `offset` is inside a file of `size` bytes,
/// asked in 64-bit arithmetic so that a length near the top of the range cannot
/// wrap around into looking valid.
[[nodiscard]] bool within(uint64_t offset, uint64_t count, uint64_t size) {
    return offset <= size && count <= size - offset;
}

}  // namespace

Result<NodelistDb> NodelistDb::open(const std::string& path) {
    const auto isFile = config::text::insistItIsAFile(path);
    if (!isFile) return tl::make_unexpected(isFile.error());

    std::ifstream in(path, std::ios::binary);
    if (!in) return failure("compiled nodelist not found: " + path);

    NodelistDb db;
    db.path_ = path;
    db.data_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad()) return failure("cannot read the compiled nodelist: " + path);

    const auto* raw = db.data_.data();
    const uint64_t size = db.data_.size();
    if (size < format::kHeaderSize ||
        std::memcmp(raw, format::kMagic, sizeof(format::kMagic)) != 0) {
        return failure(
            path +
            " is not a compiled nodelist — nodelist_db names the "
            "file AmberEdit compiles them into, not a nodelist itself");
    }

    const uint16_t version = readU16(raw + 8);
    if (version != format::kVersion) {
        return failure(
            path + " was compiled as format version " + std::to_string(version) +
            ", and this AmberEdit reads "
            "version " +
            std::to_string(format::kVersion) + " — amberedit --compile writes it again");
    }

    db.nodeCount_ = readU32(raw + 12);
    db.recordsOffset_ = readU32(raw + 16);
    db.recordsSize_ = readU32(raw + 20);
    db.addressIndexOffset_ = readU32(raw + 24);
    db.nameCount_ = readU32(raw + 28);
    db.nameIndexOffset_ = readU32(raw + 32);
    db.namePoolOffset_ = readU32(raw + 36);
    db.namePoolSize_ = readU32(raw + 40);
    const uint32_t sourceCount = readU32(raw + 44);
    const uint32_t sourceOffset = readU32(raw + 48);
    db.builtAt_ = static_cast<std::time_t>(readU64(raw + 52));

    // Everything the searches will walk is checked here and nowhere else: a
    // truncated file must fail at open, where the path can be named, rather
    // than reading past the end of what was read in the middle of a search.
    const bool sane =
        within(db.addressIndexOffset_,
               static_cast<uint64_t>(db.nodeCount_) * format::kAddressEntrySize, size) &&
        within(db.nameIndexOffset_,
               static_cast<uint64_t>(db.nameCount_) * format::kNameEntrySize, size) &&
        within(db.namePoolOffset_, db.namePoolSize_, size) &&
        within(db.recordsOffset_, db.recordsSize_, size) && within(sourceOffset, 0, size);
    if (!sane) {
        return failure("the compiled nodelist is truncated or damaged: " +
                                 path + " — amberedit --compile writes it again");
    }
    // The pool is read as C strings, so the last one has to end.
    if (db.namePoolSize_ != 0 && raw[db.namePoolOffset_ + db.namePoolSize_ - 1] != '\0') {
        return failure("the compiled nodelist is damaged: " + path +
                                 " — amberedit --compile writes it again");
    }

    uint64_t at = sourceOffset;
    db.sources_.reserve(sourceCount);
    // Answers whether it could read, the complaint being the same one for every
    // way it could not: a lambda cannot return out of open() on its behalf.
    const auto readString = [&](std::string& out) {
        if (!within(at, 2, size)) return false;
        const uint16_t length = readU16(raw + at);
        at += 2;
        if (!within(at, length, size)) return false;
        out.assign(reinterpret_cast<const char*>(raw + at), length);
        at += length;
        return true;
    };
    for (uint32_t i = 0; i < sourceCount; ++i) {
        SourceState state;
        if (!readString(state.spec) || !readString(state.path) ||
            !within(at, 16, size)) {
            return failure("the compiled nodelist is truncated: " + path);
        }
        state.modified = readU64(raw + at);
        state.size = readU64(raw + at + 8);
        at += 16;
        db.sources_.push_back(std::move(state));
    }

    for (size_t i = 0; i < db.nameCount_; ++i) {
        const unsigned char* item =
            raw + db.nameIndexOffset_ + (i * format::kNameEntrySize);
        if (readU32(item) >= db.namePoolSize_ || readU32(item + 4) >= db.nodeCount_) {
            return failure("the compiled nodelist is damaged: " + path +
                           " — amberedit --compile writes it again");
        }
    }

    // And every record the searches and the dialog will read, checked here for
    // the same reason as the index arrays above: a truncated file must fail at
    // open, where the path can be named, rather than in the middle of a list
    // being drawn where there is nowhere left to say anything. That is what lets
    // addressAt(), sourceAt() and entry() be total.
    for (size_t i = 0; i < db.nodeCount_; ++i) {
        const uint64_t record =
            static_cast<uint64_t>(db.recordsOffset_) + db.recordOffsetAt(i);
        if (!within(record, format::kRecordFixedSize, size)) {
            return failure("the compiled nodelist is truncated: " + path +
                           " — amberedit --compile writes it again");
        }
        uint64_t total = 0;
        for (size_t f = 0; f < 5; ++f) total += readU16(raw + record + 14 + (f * 2));
        if (!within(record + format::kRecordFixedSize, total, size)) {
            return failure("the compiled nodelist is truncated: " + path +
                           " — amberedit --compile writes it again");
        }
    }

    return db;
}

uint64_t NodelistDb::keyAt(size_t index) const {
    return readU64(data_.data() + addressIndexOffset_ +
                   (index * format::kAddressEntrySize));
}

uint32_t NodelistDb::recordOffsetAt(size_t index) const {
    return readU32(data_.data() + addressIndexOffset_ +
                   (index * format::kAddressEntrySize) + 8);
}

domain::FtnAddress NodelistDb::addressAt(size_t index) const {
    if (index >= nodeCount_) return {};
    const uint64_t key = keyAt(index);
    return {static_cast<uint16_t>(key >> 48),
            static_cast<uint16_t>(key >> 32),
            static_cast<uint16_t>(key >> 16),
            static_cast<uint16_t>(key),
            {}};
}

size_t NodelistDb::sourceAt(size_t index) const {
    if (index >= nodeCount_) return sources_.size();
    const uint64_t at = static_cast<uint64_t>(recordsOffset_) + recordOffsetAt(index);
    return data_[at + 1];
}

NodeEntry NodelistDb::entry(size_t index) const {
    if (index >= nodeCount_) return {};

    const uint64_t at = static_cast<uint64_t>(recordsOffset_) + recordOffsetAt(index);
    const unsigned char* raw = data_.data() + at;

    NodeEntry entry;
    entry.keyword = static_cast<NodeKeyword>(raw[0]);
    entry.address = {
        readU16(raw + 2), readU16(raw + 4), readU16(raw + 6), readU16(raw + 8), {}};
    entry.speed = readU32(raw + 10);

    uint16_t lengths[5];
    for (size_t i = 0; i < 5; ++i) lengths[i] = readU16(raw + 14 + (i * 2));

    const char* text = reinterpret_cast<const char*>(raw) + format::kRecordFixedSize;
    std::string* const fields[5] = {&entry.system, &entry.location, &entry.sysop,
                                    &entry.phone, &entry.flags};
    for (size_t i = 0; i < 5; ++i) {
        fields[i]->assign(text, lengths[i]);
        text += lengths[i];
    }
    return entry;
}

size_t NodelistDb::lowerBound(uint64_t key) const {
    size_t low = 0;
    size_t high = nodeCount_;
    while (low < high) {
        const size_t middle = low + ((high - low) / 2);
        if (keyAt(middle) < key) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

std::optional<size_t> NodelistDb::find(const domain::FtnAddress& address) const {
    const uint64_t key =
        format::addressKey(address.zone, address.net, address.node, address.point);
    const size_t at = lowerBound(key);
    if (at >= nodeCount_ || keyAt(at) != key) return std::nullopt;
    return at;
}

std::optional<size_t> NodelistDb::findOrBoss(const domain::FtnAddress& address) const {
    if (const auto at = find(address)) return at;
    if (address.point == 0) return std::nullopt;

    domain::FtnAddress boss = address;
    boss.point = 0;
    return find(boss);
}

std::pair<size_t, size_t> NodelistDb::findRange(const AddressPrefix& prefix) const {
    const size_t first = lowerBound(prefix.lowKey());
    // The high key is the last one the prefix covers rather than the first one
    // past it, since the first one past 65535:65535/65535.65535 is not a number
    // — so the run ends where the keys stop being covered.
    const uint64_t high = prefix.highKey();
    // A binary search for the end too: the run may be a whole zone long.
    size_t low = first;
    size_t top = nodeCount_;
    while (low < top) {
        const size_t middle = low + ((top - low) / 2);
        if (keyAt(middle) <= high) {
            low = middle + 1;
        } else {
            top = middle;
        }
    }
    return {first, low};
}

std::vector<size_t> NodelistDb::findBySysop(std::string_view query, size_t limit,
                                            SysopOrder order) const {
    const std::string folded = foldName(query);
    if (folded.empty() || nameCount_ == 0) return {};

    const auto* pool = reinterpret_cast<const char*>(data_.data() + namePoolOffset_);
    const unsigned char* index = data_.data() + nameIndexOffset_;
    const auto compare = [&](size_t i) {
        const uint32_t offset = readU32(index + (i * format::kNameEntrySize));
        return std::strncmp(pool + offset, folded.c_str(), folded.size());
    };

    size_t low = 0;
    size_t high = nameCount_;
    while (low < high) {
        const size_t middle = low + ((high - low) / 2);
        if (compare(middle) < 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    const size_t first = low;

    high = nameCount_;
    while (low < high) {
        const size_t middle = low + ((high - low) / 2);
        if (compare(middle) <= 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }

    // One node is named by as many entries as the text appears in its name, and
    // by one for every other position that name shares — so the run is turned
    // into the set of nodes it points at, in address order.
    std::vector<size_t> nodes;
    nodes.reserve(low - first);
    for (size_t i = first; i < low; ++i) {
        nodes.push_back(readU32(index + (i * format::kNameEntrySize) + 4));
    }
    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());

    if (order == SysopOrder::Relevance) {
        // How much of the name the query accounts for, worked out from the name
        // itself rather than from the index: the index says where in the pool a
        // match is and not where in a name, and reading the name back is a copy
        // out of what is already in memory.
        std::vector<Ranked> ranked;
        ranked.reserve(nodes.size());
        for (size_t node : nodes) {
            const std::string name = foldName(entry(node).sysop);
            ranked.push_back({rankOf(name, folded), name.size(), node});
        }
        std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
            if (a.rank != b.rank) return a.rank < b.rank;
            if (a.length != b.length) return a.length < b.length;
            return a.node < b.node;
        });
        nodes.clear();
        for (const auto& item : ranked) nodes.push_back(item.node);
    }

    // The limit is the best of them and not the first of them found, so it is
    // taken after the order and never before it.
    if (limit != 0 && nodes.size() > limit) nodes.resize(limit);
    return nodes;
}

}  // namespace amberedit::nodelist
