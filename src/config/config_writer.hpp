#pragma once

#include <string>
#include <string_view>

#include "config/app_config.hpp"
#include "support/error.hpp"

namespace amberedit::config {

/// The answers a config is written out of — what `--setup` asked for, in the
/// spelling the config states it in.
///
/// Paths are what is to be written and are not touched again here: a `~/` the
/// wizard put back is a `~/` in the file, and a nodelist is already the pattern
/// it was generalized into. What this struct does insist on is that every
/// required setting has an answer, which is why there is no field for anything
/// optional but the nodelist.
struct ConfigAnswers {
    std::string userName;
    std::string address;  ///< as it is to be written; parsed before it got here
    std::string tosserConfigPath;
    TosserConfigFormat tosserFormat{TosserConfigFormat::Fidoconfig};
    std::string defaultCharset;
    std::string composeCharset;
    std::string templatePath;
    /// Empty where the nodelist step was skipped, in which case the config says
    /// nothing about a nodelist at all — and must not, since a `nodelist` line
    /// with no `nodelist_db` beside it is refused by the config itself.
    std::string nodelistPath;
    std::string nodelistDbPath;
};

/// `sample` with the answers written into the lines that state them, and every
/// other line — which is to say all the comments — passed through as it stands.
///
/// A first config is also the only documentation most people will read, and
/// `amberedit.cfg.example` is where the ninety settings nobody was asked about
/// are explained. So the wizard fills that file in rather than writing a dozen
/// bare lines: what comes out is the sample config, saying what the user said.
///
/// Fails, naming the line it wanted, where the sample no longer carries exactly
/// one of a setting this has to edit. That makes the sample a build input with a
/// shape this file depends on, which is a real coupling and a deliberate one:
/// the alternative is appending the key at the end, and a config naming `name`
/// twice is refused by the config parser anyway.
[[nodiscard]] tl::expected<std::string, ErrorPtr> renderConfigFrom(
    std::string_view sample, const ConfigAnswers& answers);

/// `renderConfigFrom` over the `amberedit.cfg.example` in the binary.
[[nodiscard]] tl::expected<std::string, ErrorPtr> renderConfig(
    const ConfigAnswers& answers);

/// Renders the config, checks that what it rendered parses, writes it beside
/// `path` and renames it over — and then reads it back the way a start reads it,
/// which is the check that the template is where the answers said.
///
/// A file already at `path` is refused rather than replaced: this is the first
/// config being written, and there is no version of overwriting somebody's own
/// that is what they meant. If the read-back fails the file is taken away
/// again — a config that is there and does not load is worse than none, because
/// it is what the next `--setup` finds and refuses to run against.
[[nodiscard]] tl::expected<void, ErrorPtr> writeConfig(const std::string& path,
                                                       const ConfigAnswers& answers);

/// A value written the way the config format takes one: quoted where it holds
/// whitespace or a `#`, bare otherwise. A value holding a `"` is refused —
/// there is no escape for one in the format, so the only honest answers are to
/// refuse it or to write a file that will not parse.
[[nodiscard]] tl::expected<std::string, ErrorPtr> configValue(std::string_view value);

/// The word `tosser_config_format` states a format with.
[[nodiscard]] std::string_view formatWord(TosserConfigFormat format);

}  // namespace amberedit::config
