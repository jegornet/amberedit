#include "app/area_manager.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include "config/areas_bbs_parser.hpp"
#include "config/fidoconfig_parser.hpp"
#include "config/manual_area_source.hpp"
#include "config/squish_cfg_parser.hpp"
#include "config/text_util.hpp"
#include "i18n/i18n.hpp"
#include "msgbase/ftn_msgbase.hpp"

namespace amberedit::app {

using config::AppConfig;
using config::AreaSortCriterion;
using config::AreaSortKey;
using config::TosserConfigFormat;
using domain::AreaConfig;
using domain::AreaKind;
using domain::FtnAddress;

namespace {

/// Three-way comparison, so that a criterion says "before", "after" or "leave
/// them alone" and `descending` is one negation rather than a second lambda.
template <typename T>
constexpr int compareValues(const T& a, const T& b) {
    return a < b ? -1 : (b < a ? 1 : 0);
}

/// Names are compared as ASCII and case-insensitively, the way the quick
/// search matches them: a Cyrillic tag sorts by its bytes, which is the safe
/// answer here — folding one needs a locale, and the locale is the terminal's.
int compareNames(const std::string& a, const std::string& b) {
    const size_t common = std::min(a.size(), b.size());
    for (size_t i = 0; i < common; ++i) {
        const char left = config::text::asciiLower(a[i]);
        const char right = config::text::asciiLower(b[i]);
        if (left != right) return compareValues(left, right);
    }
    return compareValues(a.size(), b.size());
}

/// Where a kind sorts under 't': netmail, then echo, then the local ones. The
/// bad and dupe areas are local bases the tosser fills by itself, so they go
/// with local rather than among the echoes someone reads.
int typeRank(AreaKind kind) {
    switch (kind) {
        case AreaKind::Netmail: return 0;
        case AreaKind::Echo: return 1;
        case AreaKind::Local: return 2;
        case AreaKind::Bad: return 3;
        case AreaKind::Dupe: return 4;
    }
    return 5;
}

int compareAddresses(const FtnAddress& a, const FtnAddress& b) {
    if (a.zone != b.zone) return compareValues(a.zone, b.zone);
    if (a.net != b.net) return compareValues(a.net, b.net);
    if (a.node != b.node) return compareValues(a.node, b.node);
    if (a.point != b.point) return compareValues(a.point, b.point);
    return compareNames(a.domain, b.domain);
}

int compareBy(AreaSortKey key, const AreaEntry& a, const AreaEntry& b) {
    switch (key) {
        case AreaSortKey::Address:
            return compareAddresses(a.config.address, b.config.address);
        case AreaSortKey::Echoid: return compareNames(a.config.tag, b.config.tag);
        case AreaSortKey::Group: return compareNames(a.config.group, b.config.group);
        case AreaSortKey::Type:
            return compareValues(typeRank(a.config.kind), typeRank(b.config.kind));
        case AreaSortKey::Unread: return compareValues(a.unread, b.unread);
    }
    return 0;
}

}  // namespace

void sortAreas(std::vector<AreaEntry>& areas,
               const std::vector<AreaSortCriterion>& order) {
    if (order.empty()) return;

    // stable_sort is the whole of what "areas the criteria leave equal keep the
    // config's order" means: the comparator answers false both ways round for
    // such a pair, and only a stable sort promises anything about where they
    // then land.
    std::stable_sort(areas.begin(), areas.end(),
                     [&order](const AreaEntry& a, const AreaEntry& b) {
                         for (const auto& criterion : order) {
                             const int result = compareBy(criterion.key, a, b);
                             if (result != 0)
                                 return criterion.descending ? result > 0 : result < 0;
                         }
                         return false;
                     });
}

namespace {

/// The parser for the tosser config the config names, or nothing where it names
/// none — which only a config declaring areas of its own can do.
std::unique_ptr<ports::IAreaConfigSource> makeTosserSource(const AppConfig& cfg) {
    if (cfg.tosserConfigPath.empty()) return nullptr;

    switch (cfg.tosserConfigFormat) {
        case TosserConfigFormat::Fidoconfig:
            return std::make_unique<config::FidoconfigParser>(cfg.tosserConfigPath);
        case TosserConfigFormat::AreasBbs:
            return std::make_unique<config::AreasBbsParser>(cfg.tosserConfigPath);
        case TosserConfigFormat::SquishCfg:
            return std::make_unique<config::SquishCfgParser>(cfg.tosserConfigPath);
    }
    // The format is stated explicitly in the config and validated while parsing
    // it, so getting here means someone added an enum value and forgot this.
    return nullptr;
}

}  // namespace

tl::expected<std::unique_ptr<ports::IAreaConfigSource>, ErrorPtr> makeAreaSource(
    const AppConfig& cfg) {
    auto tosser = makeTosserSource(cfg);
    // A config with neither is refused while it is read, so this is a config
    // built in code — and a null source would be a crash at the first reload.
    if (!tosser && cfg.manualAreas.empty()) {
        return failure(
            _("the config names no tosser config and declares no areas of its own"));
    }
    // Unwrapped where there is nothing to add: the common config declares no
    // areas of its own, and it should reach the same parser it always did.
    if (cfg.manualAreas.empty()) return tosser;
    return std::make_unique<config::ManualAreaSource>(cfg.manualAreas, std::move(tosser));
}

AreaManager::AreaManager(std::unique_ptr<ports::IAreaConfigSource> areaSource,
                         std::unique_ptr<ports::ILastReadStore> lastRead,
                         AppConfig appConfig)
    : areaSource_(std::move(areaSource)),
      lastRead_(std::move(lastRead)),
      appConfig_(std::move(appConfig)) {}

AreaManager::~AreaManager() = default;

tl::expected<void, ErrorPtr> AreaManager::reload(const ProgressFn& onArea) {
    closeCurrentArea();

    // Built beside the list rather than into it, and put in place at the end.
    // The area list is on the screen while a rescan runs — the modal covers a
    // box in the middle of it and no more — so emptying it first would blank the
    // table for as long as the bases take to open, and an empty list draws
    // "the tosser config declares no areas", which is a lie for that moment.
    // It also means a config that has become unreadable leaves the list as it
    // was rather than throwing it away.
    std::vector<AreaEntry> loaded;

    // A failure coming back here means "the tosser config is unavailable",
    // which is fatal for startup. A failing individual area is not.
    auto sourced = areaSource_->loadAreas();
    if (!sourced) return tl::make_unexpected(std::move(sourced).error());

    for (auto& config : *sourced) {
        AreaEntry entry;
        entry.config = std::move(config);

        // Before the base is opened rather than after: what the caller shows is
        // the area being waited on, and by the time it is read the wait is over.
        if (onArea) onArea(entry.config.tag);

        // The config as it stands in this area: the file's own settings with
        // whatever area group covers the tag laid over them. Worked out once,
        // here, rather than cached — a copy of it against opening a message base
        // is nothing, and a cache would be one more thing to keep true across a
        // rescan.
        const config::AppConfig cfg = appConfig_.effectiveFor(entry.config);

        // Only fidoconfig and squish.cfg can state a per-area AKA, and even
        // there the option is optional. Where the tosser says nothing, the area
        // is presented under the user's own address — and where an area group
        // names one, that outranks the tosser: it is the answer written about
        // this area in particular.
        const auto groups = appConfig_.groupsFor(entry.config);
        const bool groupStatesAddress = std::any_of(
            groups.begin(), groups.end(),
            [](const config::AreaGroup* group) { return group->states("address"); });
        if ((groupStatesAddress || !entry.config.address.isValid()) && cfg.userAddress) {
            entry.config.address = *cfg.userAddress;
        }

        if (entry.config.isPassthrough()) {
            entry.error = "passthrough";
            loaded.push_back(std::move(entry));
            continue;
        }

        msgbase::FtnMsgBase base(cfg.defaultCharset);
        if (const auto opened = base.open(entry.config); !opened) {
            entry.error = opened.error()->message();
            loaded.push_back(std::move(entry));
            continue;
        }

        readCounts(entry, base);
        loaded.push_back(std::move(entry));
    }

    // Last, not per area: sorting by unread needs the counts, and they are only
    // all known once every base has been opened.
    sortAreas(loaded, appConfig_.areaListSort);
    areas_ = std::move(loaded);
    return {};
}

tl::expected<ports::IMsgBase*, ErrorPtr> AreaManager::openArea(const AreaConfig& area) {
    closeCurrentArea();

    // In the charset this area is read in, which an area group may have a word
    // about — the same answer reload() opened it with.
    auto base = std::make_unique<msgbase::FtnMsgBase>(
        appConfig_.effectiveFor(area).defaultCharset);
    if (auto opened = base->open(area); !opened) {
        // Nothing on disk at all is the ordinary state of an area the tosser
        // config declares and no tosser has yet written into: the base is made
        // here and opened again, so that the first message can be written from
        // inside it. Only here, on the way into an area someone has asked for —
        // reload() opens every base there is, and creating them all at startup
        // would write a spool nobody asked for.
        //
        // A base that is half there or there and unreadable is reported as it
        // stands. An empty one written over it would take whatever it holds
        // with it, and that is not a reader's to do.
        //
        // The open() that just failed already walked the file system to find
        // this out, and says so in the error: asking it beats probing a second
        // time, which is what this did while the error was only a sentence.
        const auto* why = dynamic_cast<const MsgBaseError*>(opened.error().get());
        if (why == nullptr || why->kind() != MsgBaseError::Kind::Absent) {
            return tl::make_unexpected(std::move(opened).error());
        }
        if (auto made = base->create(area); !made) {
            return tl::make_unexpected(std::move(made).error());
        }
        if (auto again = base->open(area); !again) {
            return tl::make_unexpected(std::move(again).error());
        }
    }
    currentBase_ = std::move(base);
    currentArea_ = area;
    return currentBase_.get();
}

void AreaManager::readCounts(AreaEntry& entry, ports::IMsgBase& base) const {
    entry.total = base.count();
    // The mark on disk is a UID; what "unread" counts is how many messages sit
    // after it, so it has to be placed in the area first. A mark on a message
    // that has since been packed away lands on the one before it.
    const uint32_t uid = lastRead_->getLastRead(entry.config);
    const uint32_t readIndex = uid == 0 ? 0 : base.indexOfUid(uid);
    entry.unread = readIndex >= entry.total ? 0 : entry.total - readIndex;
}

void AreaManager::refreshArea(const AreaConfig& area) {
    const auto found =
        std::find_if(areas_.begin(), areas_.end(), [&area](const AreaEntry& entry) {
            // Tag and path together name an area, as everywhere else here.
            return entry.config.tag == area.tag && entry.config.path == area.path;
        });
    if (found == areas_.end() || found->config.isPassthrough()) return;

    // The base already open on it, rather than a second handle on the same
    // files: the base locks what it writes to, and a fresh handle would have
    // nothing to say that this one does not.
    if (currentBase_ && currentArea_.tag == area.tag && currentArea_.path == area.path) {
        // A base that opened is not the unavailable area the list may still be
        // drawing dimmed: an area entered for the first time has just had its
        // base made, and the row saying it could not be read is out of date.
        found->error.clear();
        readCounts(*found, *currentBase_);
        return;
    }

    msgbase::FtnMsgBase base(appConfig_.effectiveFor(area).defaultCharset);
    // An area that will not open keeps the counts it had, and the error it was
    // found with. It is already in the list as it was at startup, and a base
    // busy for a moment is not news worth replacing either with.
    if (!base.open(found->config)) return;
    found->error.clear();
    readCounts(*found, base);
}

uint32_t AreaManager::startingMessage(const AreaConfig& area, uint32_t messageCount) {
    if (messageCount == 0) return 0;

    const uint32_t uid = lastRead_->getLastRead(area);
    // No mark at all means nothing here has been read, and the first unread
    // message of an area nobody has read is its first one. That is where the
    // reading starts: an area is read forwards, and the newest message is the
    // far end of everything still unread rather than the near one. It is also
    // what ← off the front of an area asks for — walking off there takes the
    // mark away, and coming back in has to land where the walking stopped.
    if (uid == 0) return 1;
    // Only the open base can turn a UID into a position. Without one there is
    // nothing to convert against, and the newest message is the honest default.
    if (currentBase_) {
        const uint32_t index = currentBase_->indexOfUid(uid);
        // A mark older than anything the base still holds has no message before
        // it to land on: everything in the area stands unread, the front again.
        if (index == 0) return 1;
        if (index <= messageCount) {
            // The mark names the message last read, so the first one wanted is
            // the one after it — what `reader_lastread_auto_next` asks for.
            // On the newest message there is nothing after it to move to, and
            // the area opens on the marked message whichever way the setting
            // stands.
            if (appConfig_.lastreadAutoNext && index < messageCount) return index + 1;
            return index;
        }
    }
    return messageCount;
}

void AreaManager::markRead(uint32_t index) {
    if (!currentBase_ || index == 0 || index > currentBase_->count()) return;

    // A position is only meaningful while the base is open — store what will
    // still name this message after the next pack.
    const uint32_t uid = currentBase_->uidOf(index);
    if (uid == 0) return;

    lastRead_->setLastRead(currentArea_, uid);
    updateUnread(currentArea_, index);
}

void AreaManager::markUnread() {
    if (!currentBase_) return;

    // Zero is what all three formats keep for "this user has read nothing
    // here", so unlike a mark on a message there is no UID to work out first.
    lastRead_->setLastRead(currentArea_, 0);
    updateUnread(currentArea_, 0);
}

void AreaManager::updateUnread(const AreaConfig& area, uint32_t readIndex) {
    for (auto& entry : areas_) {
        if (entry.config.tag != area.tag || entry.config.path != area.path) continue;
        entry.unread = readIndex >= entry.total ? 0 : entry.total - readIndex;
        return;
    }
}

void AreaManager::closeCurrentArea() {
    if (currentBase_) currentBase_->close();
    currentBase_.reset();
    currentArea_ = {};
}

}  // namespace amberedit::app
