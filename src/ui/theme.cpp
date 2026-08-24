#include "ui/theme.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
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

const std::array<std::pair<std::string_view, Field>, 39> kFields{{
    {"background", &Palette::background},
    {"selection", &Palette::selection},
    {"selection_text", &Palette::selectionText},
    {"reader_sidebar_msglist_selected", &Palette::readerSidebarMsglistSelected},
    {"input_field", &Palette::inputField},
    {"input_text", &Palette::inputText},
    {"focused_field", &Palette::focusedField},
    {"focused_text", &Palette::focusedText},
    {"input_filler", &Palette::inputFiller},
    {"dialog_background", &Palette::dialogBackground},
    {"dialog_text", &Palette::dialogText},
    {"dialog_title", &Palette::dialogTitle},
    {"dialog_label", &Palette::dialogLabel},
    {"dialog_hint", &Palette::dialogHint},
    {"dialog_field", &Palette::dialogField},
    {"dialog_flash", &Palette::dialogFlash},
    {"dialog_border", &Palette::dialogBorder},
    {"dialog_shadow", &Palette::dialogShadow},
    {"header", &Palette::header},
    {"own_name", &Palette::ownName},
    {"msglist_unread", &Palette::msglistUnread},
    {"text", &Palette::text},
    {"link", &Palette::link},
    {"quote_even", &Palette::quoteEven},
    {"quote_odd", &Palette::quoteOdd},
    {"kludge", &Palette::kludge},
    {"screen_buttons", &Palette::screenButtons},
    {"dimmed", &Palette::dimmed},
    {"scroll_thumb", &Palette::scrollThumb},
    {"trailer", &Palette::trailer},
    {"table_header", &Palette::tableHeader},
    {"menu_button", &Palette::menuButton},
    {"hint_bar", &Palette::hintBar},
    {"separator", &Palette::separator},
    {"scroll_track", &Palette::scrollTrack},
    {"error", &Palette::error},
    {"unsent", &Palette::unsent},
    {"found", &Palette::found},
    {"animated_button_text", &Palette::animatedButtonText},
}};

/// The keys that are not colors, the same way round: the name in the file
/// against the field it fills. One so far — `input_filler_show` — and a table
/// rather than an `if`, so that a second one is a line here as a color is a line
/// above.
using Switch = bool Palette::*;

const std::array<std::pair<std::string_view, Switch>, 1> kSwitches{{
    {"input_filler_show", &Palette::inputFillerShown},
}};

Result<Palette> fromEntries(const std::vector<config::CfgEntry>& entries) {
    Palette palette;

    for (const auto& entry : entries) {
        const auto setting = std::find_if(
            kSwitches.begin(), kSwitches.end(),
            [&entry](const auto& known) { return known.first == entry.key; });
        if (setting != kSwitches.end()) {
            auto on = entry.flag();
            if (!on) return tl::make_unexpected(std::move(on).error());
            palette.*(setting->second) = *on;
            continue;
        }

        const auto field =
            std::find_if(kFields.begin(), kFields.end(),
                         [&entry](const auto& role) { return role.first == entry.key; });
        if (field == kFields.end()) {
            return entry.fail("'" + entry.key +
                              "' is not a color or a setting this theme knows");
        }

        auto value = entry.one();
        if (!value) return tl::make_unexpected(std::move(value).error());

        // Said with the palette named rather than as a bare "not a number": a
        // theme file is written by hand, and "#rrggbb" is what one predating
        // palette numbers still has in it.
        const auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
        if (value->empty() || !std::all_of(value->begin(), value->end(), isDigit)) {
            return entry.fail(
                entry.key + " must be a palette number from 0 to 255, not '" + *value +
                "' — themes are written in the terminal's 256-color palette "
                "rather than in #rrggbb");
        }
        auto number = entry.numberIn(0, 255);
        if (!number) return tl::make_unexpected(std::move(number).error());
        palette.*(field->second) = Color{static_cast<uint8_t>(*number)};
    }
    return palette;
}

}  // namespace

Result<Palette> parsePalette(const std::string& text, const std::string& originName) {
    auto entries = config::parseCfg(text, originName);
    if (!entries) return tl::make_unexpected(std::move(entries).error());
    return fromEntries(*entries);
}

Result<Palette> loadPalette(const std::string& path) {
    const auto text = config::text::readFile(path);
    // Named as a theme rather than as a file: it is the config's `theme` line
    // that sent us here, and that is where the answer is.
    if (!text)
        return failure("cannot read theme " + path + ": " + text.error()->message());
    return parsePalette(*text, path);
}

}  // namespace amberedit::ui::theme
