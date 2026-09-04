#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace amberedit::config {

/// One `map_path` line: a path as the tosser config writes it, and the path the
/// same directory is at on this machine.
struct PathMapping {
    std::string source;
    std::string target;
};

/// The `map_path` lines, and the one thing they do: rewrite a path AmberEdit
/// read out of a *tosser* config into the path it is at here.
///
/// It is for the config of a tosser that runs — or once ran — somewhere else:
/// under DOS or Windows, or on a machine whose base directory is mounted at
/// another point here. `map_path c:\fido /mnt/fido` makes `c:\fido\msgbase\test`
/// into `/mnt/fido/msgbase/test`, so the areas open without the tosser's config
/// being edited into something the tosser itself would not read.
///
/// Nothing AmberEdit's own config names comes through here — its paths are
/// written by whoever runs AmberEdit, on the machine it runs on, and a rule
/// meant for a DOS tosser silently rewriting `tmpdir` or an `area ... endarea`
/// block would be a trap rather than a convenience.
class PathMap {
public:
    /// Adds a rule. `source` is a path in the tosser's spelling, drive letter
    /// and backslashes and all; `target` is one in this machine's.
    void add(std::string source, std::string target);

    [[nodiscard]] bool empty() const { return rules_.empty(); }
    [[nodiscard]] const std::vector<PathMapping>& rules() const { return rules_; }

    /// Whether a rule already maps this source — asked of a path and not of the
    /// text, so that `c:\fido` and `C:/FIDO/` count as the one source they are.
    [[nodiscard]] bool maps(std::string_view source) const;

    /// `path` under the rule that matches it, or `path` itself where none does:
    /// a config may map one branch of a tosser's tree and leave the rest alone,
    /// and a path nothing was said about is a path to be opened as written.
    ///
    /// A rule matches whole components only — `c:\fido` covers `c:\fido\msgbase`
    /// and never `c:\fidonet` — comparing them without regard to case and with
    /// `/` and `\` the same character, which is what makes one rule cover a
    /// config written in either spelling. What is left of the path after the
    /// match is joined onto the target with the target's own separator, so a
    /// DOS tail arrives spelled the way this machine spells a path.
    ///
    /// Where two rules both match, the one pinning down more of the path wins:
    /// mapping `c:\fido` in general and `c:\fido\msgbase` to somewhere of its
    /// own is the ordinary reason to write two, and reading them in the order
    /// they happen to stand in would make the general one hide the particular.
    [[nodiscard]] std::string apply(const std::string& path) const;

private:
    std::vector<PathMapping> rules_;
};

}  // namespace amberedit::config
