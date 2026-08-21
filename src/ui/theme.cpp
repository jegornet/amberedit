#include "ui/theme.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/cfg_file.hpp"
#include "config/text_util.hpp"

namespace amberedit::ui::theme {

namespace {

/// The theme file's keys, each paired with the Palette field it fills. Kept as
/// one table so that the file format and the struct cannot drift apart: adding
/// a role means adding a line here, and an unknown key is whatever is not in
/// it.
using Field = Color Palette::*;

const std::array<std::pair<std::string_view, Field>, 26> kFields{{
    {"background", &Palette::background},
    {"selection", &Palette::selection},
    {"selection_text", &Palette::selectionText},
    {"input_field", &Palette::inputField},
    {"header", &Palette::header},
    {"own_name", &Palette::ownName},
    {"msglist_unread", &Palette::msglistUnread},
    {"text", &Palette::text},
    {"link", &Palette::link},
    {"quote_even", &Palette::quoteEven},
    {"quote_odd", &Palette::quoteOdd},
    {"kludge", &Palette::kludge},
    {"footer", &Palette::footer},
    {"dimmed", &Palette::dimmed},
    {"scroll_thumb", &Palette::scrollThumb},
    {"trailer", &Palette::trailer},
    {"table_header", &Palette::tableHeader},
    {"menu_button", &Palette::menuButton},
    {"hint_bar", &Palette::hintBar},
    {"separator", &Palette::separator},
    {"scroll_track", &Palette::scrollTrack},
    {"warning", &Palette::warning},
    {"error", &Palette::error},
    {"unsent", &Palette::unsent},
    {"found", &Palette::found},
    {"animated_button_text", &Palette::animatedButtonText},
}};

Palette fromEntries(const std::vector<config::CfgEntry>& entries) {
    Palette palette;

    for (const auto& entry : entries) {
        const auto field =
            std::find_if(kFields.begin(), kFields.end(),
                         [&entry](const auto& role) { return role.first == entry.key; });
        if (field == kFields.end()) {
            entry.fail("'" + entry.key + "' is not a color this theme knows");
        }

        // Said with the palette named rather than as a bare "not a number": a
        // theme file is written by hand, and "#rrggbb" is what one predating
        // palette numbers still has in it.
        const std::string& value = entry.one();
        const auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
        if (value.empty() || !std::all_of(value.begin(), value.end(), isDigit)) {
            entry.fail(entry.key + " must be a palette number from 0 to 255, not '" +
                       value +
                       "' — themes are written in the terminal's 256-color palette "
                       "rather than in #rrggbb");
        }
        palette.*(field->second) = Color{static_cast<uint8_t>(entry.numberIn(0, 255))};
    }
    return palette;
}

}  // namespace

Palette parsePalette(const std::string& text, const std::string& originName) {
    return fromEntries(config::parseCfg(text, originName));
}

Palette loadPalette(const std::string& path) {
    std::string text;
    try {
        text = config::text::readFile(path);
    } catch (const std::exception& e) {
        // Named as a theme rather than as a file: it is the config's `theme`
        // line that sent us here, and that is where the answer is.
        throw std::runtime_error("cannot read theme " + path + ": " + e.what());
    }
    return fromEntries(config::parseCfg(text, path));
}

}  // namespace amberedit::ui::theme
