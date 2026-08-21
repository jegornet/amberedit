#include "nodelist/nodelist_compiler.hpp"

#include <ctime>
#include <ostream>
#include <utility>

#include "nodelist/nodelist_db.hpp"
#include "nodelist/nodelist_parser.hpp"
#include "nodelist/nodelist_source.hpp"
#include "nodelist/nodelist_writer.hpp"

namespace amberedit::nodelist {

CompileReport compileNodelists(const CompileOptions& options, std::ostream* log) {
    CompileReport report;
    if (options.sources.empty()) return report;
    if (options.dbPath.empty()) {
        report.problems.emplace_back(
            "nodelist_db is not set — it is the file the nodelist lines are compiled "
            "into");
        return report;
    }

    // Every archive it unpacks is taken away again when this goes, whichever
    // way out is taken.
    NodelistSources reader(options.tempDir);
    std::vector<DbSource> compiled;
    compiled.reserve(options.sources.size());

    for (const auto& spec : options.sources) {
        CompiledSource summary;
        std::vector<NodeEntry> entries;

        auto read = reader.read(spec);
        if (!read) {
            // The state is taken anyway — what the spec names now, even where
            // that is nothing — so that the compiled file records this attempt
            // and the next start tries again only once the file itself has
            // changed. A nodelist that is not there is not an error here: it is
            // a nodelist that is not there.
            summary.state = stateOf(spec);
            summary.problem = read.error();
            report.problems.emplace_back(read.error());
            if (log != nullptr) {
                *log << "nodelist  " << spec << ": " << read.error() << "\n";
            }
        } else {
            NodelistSources::Loaded loaded = std::move(*read);
            ParseResult parsed = parseNodelist(loaded.text);

            summary.state = std::move(loaded.state);
            summary.archive = std::move(loaded.archive);
            summary.points = parsed.pointCount;
            summary.nodes = parsed.entries.size() - parsed.pointCount;
            summary.warnings = parsed.warnings.size();
            summary.pointList = parsed.pointList;
            entries = std::move(parsed.entries);

            if (log != nullptr) {
                *log << (summary.pointList ? "pointlist " : "nodelist  ")
                     << loaded.readFrom;
                if (!summary.archive.empty()) *log << " (from " << summary.archive << ")";
                *log << ": " << summary.nodes << " nodes";
                if (summary.points != 0) *log << ", " << summary.points << " points";
                *log << "\n";

                size_t shown = 0;
                for (const auto& warning : parsed.warnings) {
                    if (shown >= options.warningsShown) break;
                    ++shown;
                    *log << "  line " << warning.line << ": " << warning.message << "\n";
                }
                if (parsed.warnings.size() > shown) {
                    *log << "  and " << (parsed.warnings.size() - shown)
                         << " more lines that are not nodelist lines\n";
                }
            }
        }

        report.warnings += summary.warnings;
        compiled.push_back(DbSource{summary.state, std::move(entries)});
        report.sources.push_back(std::move(summary));
    }

    const auto written = writeNodelistDb(options.dbPath, compiled, std::time(nullptr));
    if (!written) {
        report.problems.emplace_back(written.error());
        if (log != nullptr) *log << written.error() << "\n";
        return report;
    }
    report.nodes = written->nodes;
    report.points = written->points;
    report.duplicates = written->duplicates;
    report.bytes = written->bytes;
    report.written = true;
    return report;
}

bool nodelistNeedsCompiling(const CompileOptions& options) {
    if (options.sources.empty() || options.dbPath.empty()) return false;

    std::vector<SourceState> now;
    now.reserve(options.sources.size());
    for (const auto& spec : options.sources) now.push_back(stateOf(spec));

    // Missing, unreadable, or written by another version of the format — all of
    // them come back out of open() as a failure, and all of them mean the same
    // thing here.
    const auto db = NodelistDb::open(options.dbPath);
    if (!db) return true;
    return db->sources() != now;
}

CompileReport refreshNodelist(const CompileOptions& options, bool force,
                              std::ostream* log) {
    if (options.sources.empty()) return {};
    if (!force && !nodelistNeedsCompiling(options)) return {};
    return compileNodelists(options, log);
}

}  // namespace amberedit::nodelist
