#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "app/area_manager.hpp"
#include "config/app_config.hpp"
#include "echolist/echolist_area_source.hpp"
#include "echolist/echolist_compiler.hpp"
#include "msgbase/msgbase_lastread_store.hpp"
#include "nodelist/nodelist_compiler.hpp"
#include "ui/app_shell.hpp"
#include "ui/keys.hpp"
#include "ui/setup/setup_run.hpp"
#include "ui/theme.hpp"
#include "version.hpp"

namespace {

void printUsage(const char* program) {
    std::cerr
        << "AmberEdit — a Fidonet mail editor.\n\n"
        << "Usage:\n  " << program << " [-c <config>] [--compile]\n  " << program
        << " --setup\n\n"
        << "Options:\n"
        << "  -c, --config <path>   path to the AmberEdit config\n"
        << "      --setup           ask what a first config should say and write\n"
        << "                        one; refused where there is a config already\n"
        << "      --compile         compile the nodelists and echolists before\n"
        << "                        starting, whether or not they look like they\n"
        << "                        have changed\n"
        << "  -h, --help            this help\n"
        << "  -V, --version         the version AmberEdit signs its messages with\n\n"
        << "Without -c the config is looked up in: $AMBEREDIT_CONFIG,\n"
        << "./amberedit.cfg, ~/.ambereditrc\n";
}

/// Compiles the nodelists the config names, where they need it.
///
/// This runs at every start and is deliberately not allowed to stop one: a
/// nodelist that has gone, or an archive that will not unpack, is said out loud
/// here and left at that. AmberEdit is a mail reader whose nodelist is a
/// convenience, and there is no version of "your nodelist is missing" that is
/// worth standing between the user and their mail.
///
/// It says nothing at all in the ordinary case, which is a compiled file that
/// is already the answer for the nodelists on disk.
void compileNodelists(const amberedit::config::AppConfig& config, bool force) {
    amberedit::nodelist::CompileOptions options;
    options.sources = config.nodelistSources;
    options.dbPath = config.nodelistDbPath;
    options.tempDir = config.tempDirPath;

    const auto report = amberedit::nodelist::refreshNodelist(options, force, &std::cout);
    if (report.written) {
        std::cout << report.nodes << " nodes";
        if (report.points != 0) std::cout << " and " << report.points << " points";
        std::cout << " into " << options.dbPath << "\n";
    }
    for (const auto& problem : report.problems) {
        std::cerr << "warning: " << problem << "\n";
    }
}

/// Compiles the echolists the config names, where they need it.
///
/// The nodelist's terms exactly, and for the nodelist's reason: this runs at
/// every start, says nothing at all in the ordinary case, and a missing or
/// unreadable echolist is a warning on the way past. An area whose description
/// an echolist would have carried is then an area shown by whatever the tosser
/// config says about it, which is the state every system was in before there
/// was an echolist at all.
void compileEcholists(const amberedit::config::AppConfig& config, bool force) {
    amberedit::echolist::CompileOptions options;
    options.sources.reserve(config.echolistSources.size());
    for (const auto& source : config.echolistSources) {
        options.sources.push_back({source.path, source.charset});
    }
    options.dbPath = config.echolistDbPath;
    options.tempDir = config.tempDirPath;

    const auto report = amberedit::echolist::refreshEcholist(options, force, &std::cout);
    if (report.written) {
        std::cout << report.areas << " areas into " << options.dbPath << "\n";
    }
    for (const auto& problem : report.problems) {
        std::cerr << "warning: " << problem << "\n";
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string configPath;
    bool forceCompile = false;
    bool setup = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        // The one line a script would want to read, on stdout and by itself:
        // the same string the tearline of every message we write carries.
        if (arg == "-V" || arg == "--version") {
            std::cout << amberedit::kProgramId << "\n";
            return 0;
        }
        // Forcing it, rather than a mode of its own: the nodelists and the
        // echolists are compiled when they have changed anyway, so what is left
        // for a flag to mean is "compile them even though they look unchanged" —
        // after which this is the same start it would have been.
        if (arg == "--compile") {
            forceCompile = true;
            continue;
        }
        // A mode of its own, and the only one: it writes a config rather than
        // reading one, so none of what the rest of this does applies to it.
        if (arg == "--setup") {
            setup = true;
            continue;
        }
        if (arg == "-c" || arg == "--config") {
            if (i + 1 >= argc) {
                std::cerr << "error: " << arg << " needs a value\n";
                return 2;
            }
            configPath = argv[++i];
            continue;
        }
        std::cerr << "error: unknown option '" << arg << "'\n";
        printUsage(argv[0]);
        return 2;
    }

    try {
        if (setup) {
            // -c says which config to read, and this reads none. Where the two
            // are written together it is not clear which of them was meant, and
            // guessing at that would be guessing at where a config goes.
            if (!configPath.empty()) {
                std::cerr << "error: --setup writes a config of its own, so it does "
                             "not take -c\n";
                printUsage(argv[0]);
                return 2;
            }
            if (const auto found =
                    amberedit::config::AppConfig::findDefaultConfigPath()) {
                // Named rather than said generally: with three places to look,
                // "there is already a config" leaves the user hunting for which.
                std::cerr << "error: there is already an AmberEdit config: " << *found
                          << "\n"
                          << "Edit it, or move it aside and run --setup again.\n";
                return 1;
            }
            return amberedit::ui::setup::runSetup(argv[0]);
        }

        if (configPath.empty()) {
            auto found = amberedit::config::AppConfig::findDefaultConfigPath();
            if (!found) {
                std::cerr << "error: no AmberEdit config found.\n"
                          << "Run " << argv[0]
                          << " --setup to be asked what one should say, or copy "
                             "amberedit.cfg.example to ./amberedit.cfg or "
                             "~/.ambereditrc.\n";
                return 1;
            }
            configPath = *found;
        }

        const auto loaded = amberedit::config::AppConfig::loadFromFile(configPath);
        if (!loaded) {
            std::cerr << "error: " << loaded.error() << "\n";
            return 1;
        }
        const amberedit::config::AppConfig& appConfig = *loaded;

        // The keyboard layout, read here so that a `keys` file that cannot be
        // read is said out loud like any other startup failure. A config naming
        // none is AmberEdit's own layout, which is what most configs are.
        amberedit::ui::KeyMap keys = amberedit::ui::KeyMap::defaults();
        if (!appConfig.keysPath.empty()) {
            auto read = amberedit::ui::KeyMap::loadFromFile(appConfig.keysPath);
            if (!read) {
                std::cerr << "error: " << read.error() << "\n";
                return 1;
            }
            keys = std::move(*read);
        }

        // And the theme, for the same reason and before anything is drawn:
        // every screen reads the palette while rendering, so a theme the config
        // names and cannot be read has to be said here or nowhere.
        if (!appConfig.themePath.empty()) {
            const auto palette = amberedit::ui::theme::loadPalette(appConfig.themePath);
            if (!palette) {
                std::cerr << "error: " << palette.error() << "\n";
                return 1;
            }
            amberedit::ui::theme::palette = *palette;
        }

        // Before the terminal is taken over, which is the only place there is
        // left to say anything about either of them.
        compileNodelists(appConfig, forceCompile);
        compileEcholists(appConfig, forceCompile);

        auto source = amberedit::app::makeAreaSource(appConfig);
        if (!source) {
            std::cerr << "error: " << source.error() << "\n";
            return 1;
        }

        amberedit::app::AreaManager manager(
            amberedit::echolist::withEcholistDescriptions(std::move(*source), appConfig),
            std::make_unique<amberedit::msgbase::MsgBaseLastReadStore>(
                appConfig.lastreadUser, appConfig.userName),
            appConfig);
        if (const auto read = manager.reload(); !read) {
            std::cerr << "error: " << read.error() << "\n";
            return 1;
        }

        return amberedit::ui::runApp(manager, appConfig, keys);
    } catch (const std::exception& e) {
        // Only startup failures reach this point (no config, unreadable tosser
        // config), which is why there is a terminal to print to. Inside the UI
        // exceptions are caught where they happen: a frame that will not draw
        // says so in place of the screen, a keystroke that throws leaves the
        // state as it was, and both go to the `error_log` where the config names
        // one.
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
