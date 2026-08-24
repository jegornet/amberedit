#include "echolist/echolist_compiler.hpp"

#include <ctime>
#include <iterator>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "echolist/echolist_db.hpp"
#include "echolist/echolist_parser.hpp"
#include "echolist/echolist_source.hpp"
#include "echolist/echolist_writer.hpp"

namespace amberedit::echolist {

CompileReport compileEcholists(const CompileOptions& options, std::ostream* log) {
    CompileReport report;
    if (options.sources.empty()) return report;
    if (options.dbPath.empty()) {
        report.problems.emplace_back(
            "echolist_db is not set — it is the file the echolist lines are compiled "
            "into");
        return report;
    }

    // Every archive it unpacks is taken away again when this goes, which is
    // just as true of the way out an exception takes.
    EcholistSources reader(options.tempDir);
    std::vector<DbSource> compiled;
    compiled.reserve(options.sources.size());

    for (const auto& spec : options.sources) {
        CompiledSource summary;
        std::vector<EchoEntry> entries;

        auto read = reader.read(spec.path, spec.charset);
        if (!read) {
            // The state is taken anyway — what the line names now, even where
            // that is nothing — so that the compiled file records this attempt
            // and the next start tries again only once the file itself has
            // changed. An echolist that is not there is not an error here: it is
            // an echolist that is not there.
            summary.state = stateOf(spec.path, spec.charset);
            summary.problem = read.error()->message();
            report.problems.emplace_back(read.error()->message());
            if (log != nullptr) {
                *log << "echolist  " << spec.path << ": " << read.error()->message()
                     << "\n";
            }
        } else {
            EcholistSources::Loaded loaded = std::move(*read);
            summary.state = std::move(loaded.state);
            summary.archive = std::move(loaded.archive);
            summary.files = loaded.parts.size();

            for (const auto& part : loaded.parts) {
                // The extension is what says which of the two shapes the file
                // is in, and an archive holds both kinds beside each other.
                ParseResult parsed = parseEcholist(part.text, formatOf(part.name));
                summary.areas += parsed.entries.size();
                summary.warnings += parsed.warnings.size();

                if (log != nullptr) {
                    *log << "echolist  " << part.readFrom;
                    if (!summary.archive.empty()) {
                        *log << " (from " << summary.archive << ")";
                    }
                    *log << ": " << parsed.entries.size() << " areas\n";

                    size_t shown = 0;
                    for (const auto& warning : parsed.warnings) {
                        if (shown >= options.warningsShown) break;
                        ++shown;
                        *log << "  line " << warning.line << ": " << warning.message
                             << "\n";
                    }
                    if (parsed.warnings.size() > shown) {
                        *log << "  and " << (parsed.warnings.size() - shown)
                             << " more lines that are not echolist lines\n";
                    }
                }

                entries.insert(entries.end(),
                               std::make_move_iterator(parsed.entries.begin()),
                               std::make_move_iterator(parsed.entries.end()));
            }
        }

        report.warnings += summary.warnings;
        compiled.push_back(DbSource{summary.state, std::move(entries)});
        report.sources.push_back(std::move(summary));
    }

    const auto written = writeEcholistDb(options.dbPath, compiled, std::time(nullptr));
    if (!written) {
        report.problems.emplace_back(written.error()->message());
        if (log != nullptr) *log << written.error()->message() << "\n";
        return report;
    }
    report.areas = written->areas;
    report.duplicates = written->duplicates;
    report.bytes = written->bytes;
    report.written = true;
    return report;
}

bool echolistNeedsCompiling(const CompileOptions& options) {
    if (options.sources.empty() || options.dbPath.empty()) return false;

    std::vector<SourceState> now;
    now.reserve(options.sources.size());
    for (const auto& spec : options.sources) {
        now.push_back(stateOf(spec.path, spec.charset));
    }

    // Missing, unreadable, or written by another version of the format — all of
    // them come back out of open() as a failure, and all of them mean the same
    // thing here.
    const auto db = EcholistDb::open(options.dbPath);
    if (!db) return true;
    return db->sources() != now;
}

CompileReport refreshEcholist(const CompileOptions& options, bool force,
                              std::ostream* log) {
    if (options.sources.empty()) return {};
    if (!force && !echolistNeedsCompiling(options)) return {};
    return compileEcholists(options, log);
}

}  // namespace amberedit::echolist
