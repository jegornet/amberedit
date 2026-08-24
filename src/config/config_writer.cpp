#include "config/config_writer.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#include "config/embedded_resources.hpp"
#include "config/text_util.hpp"

namespace amberedit::config {
namespace {

namespace fs = std::filesystem;

/// A line of the sample this edits, matched at the start of a line and nowhere
/// else.
///
/// The trailing space is load-bearing twice over: it is what keeps
/// `tosser_config ` off `tosser_config_format`, `address ` off `address_macro`
/// and `nodelist ` off `nodelist_db`.
struct Anchor {
    std::string_view prefix;
    /// What to call it when it is not there, in a sentence a reader of the
    /// build can act on.
    std::string_view what;
};

constexpr Anchor kName{"name ", "the name line"};
constexpr Anchor kAddress{"address ", "the address line"};
constexpr Anchor kTosserConfig{"tosser_config ", "the tosser_config line"};
constexpr Anchor kTosserFormat{"tosser_config_format ", "the tosser_config_format line"};
constexpr Anchor kDefaultCharset{"default_charset ", "the default_charset line"};
constexpr Anchor kComposeCharset{"compose_charset ", "the compose_charset line"};
constexpr Anchor kTemplate{"template ", "the template line"};
constexpr Anchor kOrigin{"origin ", "the origin line"};
constexpr Anchor kNodelist{"#nodelist ", "the commented nodelist samples"};
constexpr Anchor kNodelistDb{"#nodelist_db ", "the commented nodelist_db sample"};

/// Where the one line carrying the anchor is, or a failure saying which line was
/// wanted and how many the sample holds.
///
/// Exactly one, because a sample with two would leave this editing whichever
/// came first and leaving the other to be read as a second `name` — which the
/// config parser refuses, but only after the wizard has said it wrote a config.
[[nodiscard]] tl::expected<size_t, ErrorPtr> onlyLine(
    const std::vector<std::string>& lines, Anchor anchor) {
    size_t found = 0;
    size_t at = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (text::startsWith(lines[i], anchor.prefix)) {
            ++found;
            at = i;
        }
    }
    if (found == 1) return at;
    return failure("amberedit.cfg.example no longer holds one of " +
                   std::string(anchor.what) + " — it holds " + std::to_string(found));
}

/// Where the first line carrying the anchor is. For the `#nodelist` samples,
/// which are three of the same thing and where the first is the one to answer.
[[nodiscard]] tl::expected<size_t, ErrorPtr> firstLine(
    const std::vector<std::string>& lines, Anchor anchor) {
    for (size_t i = 0; i < lines.size(); ++i) {
        if (text::startsWith(lines[i], anchor.prefix)) return i;
    }
    return failure("amberedit.cfg.example no longer holds " + std::string(anchor.what));
}

/// Sets `key value` over the line the anchor names.
[[nodiscard]] tl::expected<void, ErrorPtr> setLine(std::vector<std::string>& lines,
                                                   Anchor anchor, std::string_view key,
                                                   std::string_view value) {
    auto at = onlyLine(lines, anchor);
    if (!at) return tl::make_unexpected(std::move(at).error());
    auto written = configValue(value);
    if (!written) return tl::make_unexpected(std::move(written).error());
    lines[*at] = std::string(key) + " " + *written;
    return {};
}

}  // namespace

std::string_view formatWord(TosserConfigFormat format) {
    switch (format) {
        case TosserConfigFormat::AreasBbs: return "areas.bbs";
        case TosserConfigFormat::SquishCfg: return "squish.cfg";
        case TosserConfigFormat::Fidoconfig: break;
    }
    return "fidoconfig";
}

tl::expected<std::string, ErrorPtr> configValue(std::string_view value) {
    if (value.find('"') != std::string_view::npos) {
        return failure("a value cannot hold a double quote: " + std::string(value));
    }

    bool quote = value.empty();
    for (char c : value) {
        // Whitespace because the values of a line are split on it and joined
        // back with one space, so two spaces in a name would become one; `#`
        // because a word starting with one begins a comment.
        if (text::asciiIsSpace(c) || c == '#') quote = true;
    }
    if (!quote) return std::string(value);
    return "\"" + std::string(value) + "\"";
}

tl::expected<std::string, ErrorPtr> renderConfigFrom(std::string_view sample,
                                                     const ConfigAnswers& answers) {
    std::vector<std::string> lines = text::splitLines(sample);

    // The lines to set, and then set one at a time. What the sample is missing
    // is a broken build rather than a broken config, so the first one that
    // cannot find its anchor is the answer and the rest are not worth running.
    struct Edit {
        Anchor anchor;
        std::string_view key;
        std::string_view value;
    };
    const Edit edits[] = {
        {kName, "name", answers.userName},
        {kAddress, "address", answers.address},
        {kTosserConfig, "tosser_config", answers.tosserConfigPath},
        {kTosserFormat, "tosser_config_format", formatWord(answers.tosserFormat)},
        {kDefaultCharset, "default_charset", answers.defaultCharset},
        {kComposeCharset, "compose_charset", answers.composeCharset},
        {kTemplate, "template", answers.templatePath},
    };
    for (const Edit& edit : edits) {
        auto done = setLine(lines, edit.anchor, edit.key, edit.value);
        if (!done) return tl::make_unexpected(std::move(done).error());
    }

    // The sample's origin is a joke about not having said where you are, and it
    // would go out at the foot of every echomail message this config writes.
    // Nobody asked the user for one, so the config says nothing — an origin the
    // reader has not chosen is worse than none, which AmberEdit copes with.
    if (auto at = onlyLine(lines, kOrigin); at) {
        lines[*at] = "#" + lines[*at];
    } else {
        return tl::make_unexpected(std::move(at).error());
    }

    // The nodelist is the one thing here that may go unanswered, and where it
    // does the samples stay commented: a `nodelist` line with no `nodelist_db`
    // beside it is refused by the config, and a nodelist nobody named is not
    // worth guessing at.
    if (!answers.nodelistPath.empty()) {
        auto nodelistAt = firstLine(lines, kNodelist);
        if (!nodelistAt) return tl::make_unexpected(std::move(nodelistAt).error());
        auto dbAt = onlyLine(lines, kNodelistDb);
        if (!dbAt) return tl::make_unexpected(std::move(dbAt).error());

        auto nodelist = configValue(answers.nodelistPath);
        if (!nodelist) return tl::make_unexpected(std::move(nodelist).error());
        auto db = configValue(answers.nodelistDbPath);
        if (!db) return tl::make_unexpected(std::move(db).error());

        lines[*nodelistAt] = "nodelist " + *nodelist;
        lines[*dbAt] = "nodelist_db " + *db;
    }

    std::string out;
    for (const auto& line : lines) {
        out += line;
        out += '\n';
    }
    return out;
}

tl::expected<std::string, ErrorPtr> renderConfig(const ConfigAnswers& answers) {
    return renderConfigFrom(resources::exampleConfig(), answers);
}

tl::expected<void, ErrorPtr> writeConfig(const std::string& path,
                                         const ConfigAnswers& answers) {
    auto text = renderConfig(answers);
    if (!text) return tl::make_unexpected(std::move(text).error());

    // What was rendered, read as a config: a wizard that writes a file the
    // program will not start on has failed, and this is where that is found out
    // rather than at the next start.
    if (const auto parsed = AppConfig::loadFromString(*text, path); !parsed) {
        return failure("the config that was written does not load: " +
                       parsed.error()->message());
    }

    const fs::path destination(path);
    std::error_code ec;
    if (fs::exists(destination, ec)) {
        return failure("there is already a file at " + path);
    }

    // Written beside the destination and renamed over it, as everything
    // AmberEdit writes is: a reader sees the whole file or none of it.
    const fs::path temporary =
        destination.parent_path() / (destination.filename().string() + ".new");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return failure("cannot write the config: " + temporary.string());
        out.write(text->data(), static_cast<std::streamsize>(text->size()));
        out.close();
        if (!out) {
            fs::remove(temporary, ec);
            return failure("cannot write the config: " + temporary.string());
        }
    }

    fs::rename(temporary, destination, ec);
    if (ec) {
        fs::remove(temporary, ec);
        return failure("cannot put the config at " + path + ": " + ec.message());
    }

    // And read back the way a start reads it, which is the one check that says
    // whether the template is really there. A config that is there and does not
    // load is worse than none: it is what the next `--setup` finds and refuses
    // to run against, leaving the user with neither a config nor a way to one.
    if (const auto loaded = AppConfig::loadFromFile(path); !loaded) {
        fs::remove(destination, ec);
        return failure("the config that was written does not load: " +
                       loaded.error()->message());
    }
    return {};
}

}  // namespace amberedit::config
