#include "nodelist/nodelist_compiler.hpp"

#include <ctime>
#include <exception>
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

    // Every archive it unpacks is taken away again when this goes, which is
    // just as true of the way out an exception takes.
    NodelistSources reader(options.tempDir);
    std::vector<DbSource> compiled;
    compiled.reserve(options.sources.size());

    for (const auto& spec : options.sources) {
        CompiledSource summary;
        std::vector<NodeEntry> entries;

        try {
            NodelistSources::Loaded loaded = reader.read(spec);
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
        } catch (const std::exception& e) {
            // The state is taken anyway — what the spec names now, even where
            // that is nothing — so that the compiled file records this attempt
            // and the next start tries again only once the file itself has
            // changed. A nodelist that is not there is not an error here: it is
            // a nodelist that is not there.
            summary.state = stateOf(spec);
            summary.problem = e.what();
            report.problems.emplace_back(e.what());
            if (log != nullptr) *log << "nodelist  " << spec << ": " << e.what() << "\n";
        }

        report.warnings += summary.warnings;
        compiled.push_back(DbSource{summary.state, std::move(entries)});
        report.sources.push_back(std::move(summary));
    }

    try {
        const WriteReport written =
            writeNodelistDb(options.dbPath, compiled, std::time(nullptr));
        report.nodes = written.nodes;
        report.points = written.points;
        report.duplicates = written.duplicates;
        report.bytes = written.bytes;
        report.written = true;
    } catch (const std::exception& e) {
        report.problems.emplace_back(e.what());
        if (log != nullptr) *log << e.what() << "\n";
    }
    return report;
}

bool nodelistNeedsCompiling(const CompileOptions& options) {
    if (options.sources.empty() || options.dbPath.empty()) return false;

    std::vector<SourceState> now;
    now.reserve(options.sources.size());
    for (const auto& spec : options.sources) now.push_back(stateOf(spec));

    try {
        const NodelistDb db = NodelistDb::open(options.dbPath);
        // Missing, unreadable, or written by another version of the format —
        // all of them come out of open() as an exception, and all of them mean
        // the same thing here.
        return db.sources() != now;
    } catch (const std::exception&) {
        return true;
    }
}

CompileReport refreshNodelist(const CompileOptions& options, bool force,
                              std::ostream* log) {
    if (options.sources.empty()) return {};
    if (!force && !nodelistNeedsCompiling(options)) return {};
    return compileNodelists(options, log);
}

}  // namespace amberedit::nodelist
