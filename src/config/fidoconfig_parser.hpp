#pragma once

#include <string>
#include <vector>

#include "config/path_map.hpp"
#include "domain/ftn_address.hpp"
#include "ports/i_area_source.hpp"

namespace amberedit::config {

/// One `link` block: the node it is, and what its two robots are called and
/// answer to.
///
/// Which is everything AmberEdit wants of a link — writing to somebody's
/// AreaFix is a netmail to a name at an address with a password in the subject,
/// and the four fields here are that message. What the tosser does with a link
/// besides, the packet sizes and the flavours and the export flags, is the
/// tosser's own business and is not read.
struct TosserLink {
    domain::FtnAddress aka;
    /// The name the robot answers to, which is `areafixName` / `filefixName`
    /// where the block states one and AreaFix / FileFix where it does not.
    std::string areafixName;
    std::string filefixName;
    /// What goes in the subject of a message to that robot. Empty where the
    /// link has no password: husky sends an empty subject there, and so do we.
    std::string areafixPwd;
    std::string filefixPwd;
};

/// What a fidoconfig says about the system it configures, beside its areas: who
/// runs it, what addresses it has, and what its links are.
///
/// The sysop and the addresses are settings AmberEdit's own config states as
/// `name` and `address`, and a person who has written them once for the tosser
/// should not have to write them again — see `AppConfig::userName`. The links
/// are what `address_macro_link_areafix` turns into netmail macros.
///
/// The addresses are in the order the file names them, which is the order that
/// matters: fidoconfig's first `address` is the main AKA and the rest are the
/// others. Every address here — a link's as much as ours — comes without its
/// domain: the statement is 5D and a message base holds nothing below four
/// dimensions, so `2:5020/9999@fidonet` is read as the node it names.
struct TosserSystem {
    std::string sysop;
    std::vector<domain::FtnAddress> addresses;
    std::vector<TosserLink> links;
};

/// Parser for husky/hpt-style tosser configs (fidoconfig):
///
///   EchoArea localnet /ftn/msg/localnet -b squish -a 2:5020/1
///   netmailarea NETMAIL /ftn/msg/netmail -g A -b msg
///
/// It reads EchoArea / NetmailArea / LocalArea / BadArea / DupeArea lines, the
/// `echoareadefaults` those inherit from, the `set` definitions that `[name]`
/// stands for anywhere below them, the include directive, and the statements
/// that say whose system it is and who its links are — `sysop`, `address`,
/// `link` with its `aka`, passwords and robot names, and the `linkdefaults`
/// those inherit from. `loadSystem()` is what hands that back. Everything else
/// is ignored: AmberEdit needs the area list, who the mail is from and who it
/// is written to, and does not aim to understand the whole tosser config.
///
/// Passthrough areas are left out of the list, however the config marks them —
/// `passthrough` where the base would be, the `-pass` option beside a base
/// written out in full, or a line that names no base at all under
/// `echoareadefaults passthrough`. There is nothing on disk to read.
///
/// Every path it takes out of the file — an area's base and the file an
/// `include` names — comes back through the `map_path` rules it was built with,
/// so a config written for a tosser that runs elsewhere opens here.
class FidoconfigParser final : public ports::IAreaConfigSource {
public:
    /// `charset` is `config_charset` — the charset the AmberEdit config, and
    /// so the tosser config it names, is written in. Empty means UTF-8.
    explicit FidoconfigParser(std::string path, PathMap paths = {},
                              std::string charset = {});

    [[nodiscard]] tl::expected<std::vector<domain::AreaConfig>, ErrorPtr> loadAreas()
        override;

    /// What the config says about the system besides its areas, the `include`s
    /// walked for it as they are for the areas — an HPT config commonly keeps
    /// the links in a file of their own. A config stating none of it answers
    /// with an empty name, no addresses and no links, which is not an error:
    /// the tosser's config is under no obligation to say who runs it.
    [[nodiscard]] tl::expected<TosserSystem, ErrorPtr> loadSystem();

    /// Parsing from a string — the entry point for tests and include files.
    static std::vector<domain::AreaConfig> parseText(const std::string& content,
                                                     const PathMap& paths = {});

    /// The same for the system, and for the same callers.
    static TosserSystem parseSystemText(const std::string& content);

    /// Every `include` the last `loadAreas()` passed over because the file it
    /// named is not there, in the spelling it was looked for under — mapped by
    /// `map_path` and resolved against the including file's directory, which is
    /// where a reader has to go and look.
    ///
    /// Reading goes on past such a line rather than stopping at it: a config
    /// whose optional include is missing still names areas, and a start that
    /// refused it would be AmberEdit refusing to run over a file it does not
    /// need. So the fact is kept here instead, for whoever has somebody in
    /// front of them to say out loud — `checkTosserConfig()` does, and "no
    /// areas at all" is exactly the shape this failure takes.
    [[nodiscard]] const std::vector<std::string>& missingIncludes() const {
        return missingIncludes_;
    }

private:
    std::string path_;
    PathMap paths_;
    std::string charset_;
    std::vector<std::string> missingIncludes_;
};

}  // namespace amberedit::config
