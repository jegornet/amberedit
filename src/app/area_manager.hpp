#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "config/app_config.hpp"
#include "ports/i_area_source.hpp"
#include "ports/i_lastread_store.hpp"
#include "ports/i_msgbase.hpp"

namespace amberedit::app {

/// An area together with the statistics computed for it — what the area list
/// screen displays.
struct AreaEntry {
    domain::AreaConfig config;
    uint32_t total{0};
    uint32_t unread{0};
    /// Empty if the area opened. Otherwise it says why it could not be read;
    /// such an area stays in the list but is marked unavailable.
    std::string error;

    [[nodiscard]] bool isAvailable() const { return error.empty(); }
};

/// Ties the tosser config, the message bases and the lastread marks together:
/// it hands out the area list with statistics and opens a chosen area's base.
class AreaManager {
public:
    AreaManager(std::unique_ptr<ports::IAreaConfigSource> areaSource,
                std::unique_ptr<ports::ILastReadStore> lastRead,
                config::AppConfig appConfig);
    ~AreaManager();

    /// Called with an area's tag as it is reached, for a caller that has
    /// something to show meanwhile.
    using ProgressFn = std::function<void(const std::string& tag)>;

    /// Reads the tosser config and computes statistics for every area. It fails
    /// only if the config itself is unavailable; unavailable areas end up in the
    /// list with AreaEntry::error filled in.
    ///
    /// The new list replaces the old one at the end, in one step: until then
    /// areas() answers with what it did before, which is what the area list goes
    /// on drawing while a rescan runs. A config that has become unreadable
    /// therefore leaves the list standing rather than emptying it.
    ///
    /// `onArea` is called with each area's tag before its base is opened, which
    /// is where the time goes: on an area list of any size this takes long
    /// enough that the screen has to say what it is doing. It is called from
    /// inside the loop, so whatever it does happens between two bases and not
    /// on a thread of its own.
    [[nodiscard]] tl::expected<void, ErrorPtr> reload(const ProgressFn& onArea = {});

    [[nodiscard]] const std::vector<AreaEntry>& areas() const { return areas_; }

    /// Opens an area's base and returns it, or says why it did not open.
    [[nodiscard]] tl::expected<ports::IMsgBase*, ErrorPtr> openArea(
        const domain::AreaConfig& area);

    /// Closes the currently open base, if there is one.
    void closeCurrentArea();

    /// The message the reader should open an area at (1-based): the one after
    /// the lastread mark when it points at a message that still exists, and the
    /// first message where nothing in the area has been read — no mark at all,
    /// or one older than anything the base still holds. Zero when the area
    /// holds nothing.
    ///
    /// `reader_lastread_auto_next` is what puts it after the mark rather than
    /// on it; either way the last message in the area is as far as it goes.
    ///
    /// The area must be the one currently open — the mark is stored as a UID
    /// and only the open base can say which position that is now.
    uint32_t startingMessage(const domain::AreaConfig& area, uint32_t messageCount);

    /// Reads one area's statistics again — the total and the unread count the
    /// area list shows — after something has changed the base under them: a
    /// message written into it, one taken out of it, or a tosser delivering
    /// into it while it was being read, which is what leaving an area asks
    /// about.
    ///
    /// Both counts come off the base and the mark on disk rather than being
    /// adjusted by hand, so a base another program has also written to comes
    /// back with what it actually holds and not with what this one expected.
    ///
    /// The area that is open is read through the base already open on it; any
    /// other is opened for the reading and closed again, which leaves the open
    /// one where it was. An area the list does not hold, a passthrough, or one
    /// that will not open is left exactly as it stands.
    void refreshArea(const domain::AreaConfig& area);

    /// Records that the message at this position in the open area has been
    /// read, which is what puts the mark on disk.
    ///
    /// The mark moves wherever the reader goes, backwards included: that is how
    /// every FTN reader has behaved, and it is what makes "continue where I
    /// left off" mean the message actually left off at. The area list's unread
    /// count is brought up to date at the same time, so leaving the area shows
    /// the reading that was just done rather than what was true on startup.
    void markRead(uint32_t index);

    /// Records that the open area stands read no further than its front: the
    /// mark is taken off it, and every message in it counts as unread again.
    ///
    /// This is where the reader is left standing *before* the first message —
    /// ← on the first message, which `reader_edge_exit` answers by leaving the
    /// area. The mark names the message last read, and there is none before
    /// the first one, so the honest mark there is no mark at all. Esc on the
    /// same message says only "out of here" and leaves the mark where reading
    /// put it, which is the difference between the two ways out.
    void markUnread();

private:
    /// Recomputes one area's unread count from a position within it.
    void updateUnread(const domain::AreaConfig& area, uint32_t readIndex);

    /// Fills an entry's two counts in from an open base: what it holds, and how
    /// much of that stands after the lastread mark. The mark is a UID, so it
    /// has to be placed in the area before anything can be counted from it.
    void readCounts(AreaEntry& entry, ports::IMsgBase& base) const;

    std::unique_ptr<ports::IAreaConfigSource> areaSource_;
    std::unique_ptr<ports::ILastReadStore> lastRead_;
    config::AppConfig appConfig_;

    std::vector<AreaEntry> areas_;
    std::unique_ptr<ports::IMsgBase> currentBase_;
    /// The area currentBase_ was opened for. Marking a message read needs it,
    /// and the reader has no reason to hand it back on every keystroke.
    domain::AreaConfig currentArea_;
};

/// Puts the list in the order `arealist_sort` asks for: the first criterion
/// decides and the rest break its ties, and areas all of them leave equal keep
/// the order the tosser config names them in. An empty order leaves the list
/// exactly as it came.
///
/// Sorting by unread counts is done once, when the list is built. The counts go
/// on changing as messages are read, but a list that reordered itself under the
/// cursor while someone walked down it would be unusable.
void sortAreas(std::vector<AreaEntry>& areas,
               const std::vector<config::AreaSortCriterion>& order);

/// Builds an area source from the app config, picking fidoconfig or areas.bbs
/// according to the format stated there.
[[nodiscard]] tl::expected<std::unique_ptr<ports::IAreaConfigSource>, ErrorPtr>
makeAreaSource(const config::AppConfig& cfg);

}  // namespace amberedit::app
