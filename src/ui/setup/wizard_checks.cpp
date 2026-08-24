#include "ui/setup/wizard_checks.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#include "config/areas_bbs_parser.hpp"
#include "config/config_writer.hpp"
#include "config/embedded_resources.hpp"
#include "config/fidoconfig_parser.hpp"
#include "config/squish_cfg_parser.hpp"
#include "config/text_util.hpp"
#include "domain/area.hpp"
#include "encoding/charset_detector.hpp"
#include "encoding/iconv_recoder.hpp"
#include "i18n/i18n.hpp"

namespace amberedit::ui::setup {
namespace {

namespace fs = std::filesystem;
namespace text = config::text;  // no term::text here, so the short name is free

/// The one thing no answer may hold: the config format has no escape for a
/// double quote, and a value carrying one cannot be written at all.
[[nodiscard]] tl::expected<void, ErrorPtr> withoutQuotes(std::string_view typed,
                                                         const char* what) {
    if (typed.find('"') != std::string_view::npos) {
        return failure(i18n::format(_("{0} cannot hold a double quote"), {what}));
    }
    return {};
}

/// The name a format's config is required to have, or empty where it may have
/// any — a fidoconfig is called `config` on one machine and `fidoconfig` on the
/// next, and HPT is told which by its own command line.
[[nodiscard]] std::string_view requiredName(config::TosserConfigFormat format) {
    switch (format) {
        case config::TosserConfigFormat::AreasBbs: return "areas.bbs";
        case config::TosserConfigFormat::SquishCfg: return "squish.cfg";
        case config::TosserConfigFormat::Fidoconfig: break;
    }
    return {};
}

}  // namespace

tl::expected<void, ErrorPtr> checkName(std::string_view typed) {
    if (text::trim(typed).empty()) {
        return failure(
            _("a name is what a message you write is from — it cannot be empty"));
    }
    return withoutQuotes(typed, _("a name"));
}

tl::expected<domain::FtnAddress, ErrorPtr> checkAddress(std::string_view typed) {
    const std::string_view trimmed = text::trim(typed);
    if (trimmed.empty()) return failure(_("an address is required, as 2:5020/9999"));

    const auto parsed = domain::FtnAddress::parse(trimmed);
    if (!parsed || !parsed->isValid()) {
        return failure(i18n::format(
            _("'{0}' is not an FTN address — it is written zone:net/node, as "
              "2:5020/9999 or 2:5020/9999.1"),
            {trimmed}));
    }
    return *parsed;
}

tl::expected<void, ErrorPtr> checkCharsetAnswer(std::string_view typed) {
    const std::string charset(text::trim(typed));
    if (charset.empty()) return failure(_("a charset is required, as CP866"));
    for (char c : charset) {
        // A charset is read as a single value, so a second word would be
        // refused by the config with less to say about it than this.
        if (text::asciiIsSpace(c)) {
            return failure(i18n::format(
                _("a charset is one word — '{0}' is more than one"), {charset}));
        }
    }
    if (auto quotes = withoutQuotes(charset, _("a charset")); !quotes)
        return tl::make_unexpected(std::move(quotes).error());

    // Through AmberEdit's own alias table first, and iconv second: the config
    // takes the names Fidonet writes — LATIN-1, +7_FIDO — and it is what those
    // stand for that iconv is asked about. `IBMPC` stands for nothing in
    // particular and normalizes to nothing, which is what this catches.
    const std::string normalized = encoding::CharsetDetector::normalize(charset);
    if (normalized.empty()) {
        return failure(i18n::format(
            _("'{0}' names no charset in particular — say which one, as CP866"),
            {charset}));
    }
    return encoding::checkCharset(normalized);
}

std::string defaultReadCharset(const domain::FtnAddress& address) {
    // The normalized spelling and not the typed one: `02:5020/1` is the same
    // address and would not start with `2:50`.
    const std::string written = address.toString();
    if (text::startsWith(written, "2:50") || text::startsWith(written, "2:60")) {
        return "CP866";
    }
    if (address.zone == 2) return "LATIN-1";
    return "CP437";
}

tl::expected<size_t, ErrorPtr> checkTosserConfig(const std::string& path,
                                                 config::TosserConfigFormat format) {
    if (path.empty()) return failure(_("no tosser config is named"));
    if (auto isFile = text::insistItIsAFile(path); !isFile) {
        return tl::make_unexpected(std::move(isFile).error());
    }
    if (auto quotes = withoutQuotes(path, _("a path")); !quotes) {
        return tl::make_unexpected(std::move(quotes).error());
    }

    tl::expected<std::vector<domain::AreaConfig>, ErrorPtr> areas = failure("");
    switch (format) {
        case config::TosserConfigFormat::Fidoconfig:
            areas = config::FidoconfigParser(path).loadAreas();
            break;
        case config::TosserConfigFormat::AreasBbs:
            areas = config::AreasBbsParser(path).loadAreas();
            break;
        case config::TosserConfigFormat::SquishCfg:
            areas = config::SquishCfgParser(path).loadAreas();
            break;
    }
    if (!areas) return tl::make_unexpected(std::move(areas).error());

    // An empty answer is nearly always the wrong file or the wrong format, and
    // the user is standing right here to say which.
    if (areas->empty()) {
        return failure(i18n::format(_("no areas in {0} — is it really a {1}?"),
                                    {path, config::formatWord(format)}));
    }
    return areas->size();
}

bool acceptsTosserFile(config::TosserConfigFormat format, std::string_view name) {
    const std::string_view wanted = requiredName(format);
    if (wanted.empty()) return true;
    return text::iequals(name, wanted);
}

std::string defaultNodelistDb(const std::string& nodelistPath) {
    if (nodelistPath.empty()) return {};
    fs::path beside(nodelistPath);
    beside.replace_filename("amberndl.db");
    return beside.string();
}

std::optional<std::string> probeTemplate(const std::string& programPath) {
    std::vector<fs::path> candidates{
        "/usr/share/amberedit/default.tpl",
        "/usr/local/share/amberedit/default.tpl",
        "default.tpl",
    };
    // And where the program itself is, which is what a build tree looks like —
    // and, with the `..` above it, an install somebody moved.
    const fs::path program(programPath);
    if (program.has_parent_path()) {
        const fs::path directory = program.parent_path();
        candidates.push_back(directory / "default.tpl");
        candidates.push_back(directory / ".." / "share" / "amberedit" / "default.tpl");
    }

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (!fs::is_regular_file(candidate, ec)) continue;
        std::ifstream in(candidate);
        if (!in) continue;

        // Absolute, because a config saying `default.tpl` would be read against
        // whatever directory AmberEdit is started in next time.
        const fs::path absolute = fs::weakly_canonical(candidate, ec);
        return ec ? candidate.string() : absolute.string();
    }
    return std::nullopt;
}

tl::expected<std::string, ErrorPtr> ensureTemplate(const std::string& configPath,
                                                   const std::string& programPath) {
    if (const auto installed = probeTemplate(programPath)) return *installed;

    const fs::path beside = fs::path(configPath).parent_path() / "default.tpl";
    std::error_code ec;
    if (fs::exists(beside, ec)) {
        // The probe has already tried to read it, so if it is here and was not
        // taken it cannot be read — and it is somebody's file either way.
        return failure(i18n::format(
            _("there is a default.tpl at {0} that cannot be read — the config "
              "needs a template it can"),
            {beside.string()}));
    }

    const std::string_view content = config::resources::defaultTemplate();
    std::ofstream out(beside, std::ios::binary | std::ios::trunc);
    if (!out) {
        return failure(
            i18n::format(_("cannot write the message template: {0}"), {beside.string()}));
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    if (!out) {
        fs::remove(beside, ec);
        return failure(
            i18n::format(_("cannot write the message template: {0}"), {beside.string()}));
    }

    const fs::path absolute = fs::weakly_canonical(beside, ec);
    return ec ? beside.string() : absolute.string();
}

tl::expected<void, ErrorPtr> checkTargetPath(const std::string& path) {
    if (text::trim(path).empty()) {
        return failure(_("say where the config is to be written"));
    }
    if (auto quotes = withoutQuotes(path, _("a path")); !quotes)
        return tl::make_unexpected(std::move(quotes).error());

    const fs::path target(path);
    std::error_code ec;
    if (fs::exists(target, ec)) {
        return failure(fs::is_directory(target, ec)
                           ? i18n::format(_("{0} is a directory"), {path})
                           : i18n::format(_("there is already a file at {0}"), {path}));
    }

    const fs::path directory =
        target.has_parent_path() ? target.parent_path() : fs::path(".");
    if (!fs::is_directory(directory, ec)) {
        return failure(
            i18n::format(_("there is no directory {0}"), {directory.string()}));
    }
    return {};
}

std::string abbreviateHome(const std::string& path) {
    const char* home = std::getenv("HOME");
    if (home == nullptr) return path;

    std::string prefix(home);
    // A `$HOME` of `/` would make every path on the machine a `~/` one, and a
    // trailing slash would leave a `~//` behind.
    while (prefix.size() > 1 && prefix.back() == '/') prefix.pop_back();
    if (prefix.size() < 2) return path;

    if (path.size() > prefix.size() && text::startsWith(path, prefix) &&
        path[prefix.size()] == '/') {
        return "~" + path.substr(prefix.size());
    }
    return path;
}

}  // namespace amberedit::ui::setup
