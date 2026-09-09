#include "ui/theme.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/cfg_file.hpp"
#include "config/text_util.hpp"
#include "i18n/i18n.hpp"

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
/// against the field it fills. A table rather than an `if`, so that another one
/// is a line here as a color is a line above.
using Switch = bool Palette::*;

const std::array<std::pair<std::string_view, Switch>, 2> kSwitches{{
    {"input_filler_show", &Palette::inputFillerShown},
    {"selection_bold", &Palette::selectionBold},
}};

tl::expected<Palette, ErrorPtr> fromEntries(
    const std::vector<config::CfgEntry>& entries) {
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

        // A role with nothing after it is nearly always a color written with a
        // '#' in front of it: that opens a comment, and the comment takes the
        // color with it. Answered here rather than left to `one()`, whose
        // "takes exactly one value" would say nothing about where the value went.
        if (entry.values.empty()) {
            return entry.fail(entry.key +
                              " has no color after it — a color is written "
                              "without a '#', which starts a comment here");
        }

        auto value = entry.one();
        if (!value) return tl::make_unexpected(std::move(value).error());

        // Which of the two a color is, is settled by how it is written and by
        // nothing else: exactly six hex digits is the color itself, one to three
        // decimal digits an entry in the terminal's palette. The lengths cannot
        // overlap, so `1c1e2a` and `232` each mean one thing — and no mark is
        // needed in front of either, `#` being what opens a comment in every
        // file AmberEdit reads.
        const auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
        const bool truecolor =
            value->size() == 6 &&
            std::all_of(value->begin(), value->end(), config::text::asciiIsHexDigit);

        // What a terminal makes of the triple is `term::pairFor`'s business, and
        // nothing here or there can ask it whether it means 24-bit color: a
        // theme written in colors is the user's own risk, and is not refused.
        if (truecolor) {
            uint32_t triple = 0;
            std::from_chars(value->data(), value->data() + value->size(), triple, 16);
            palette.*(field->second) = Color::rgb(triple);
            continue;
        }

        // Said with both spellings named rather than as a bare "not a number":
        // a theme file is written by hand, and the two are the whole of what a
        // color may be.
        if (value->empty() || value->size() > 3 ||
            !std::all_of(value->begin(), value->end(), isDigit)) {
            return entry.fail(entry.key +
                              " must be a palette number from 0 to 255, or six hex "
                              "digits for the color itself as 1c1e2a is, not '" +
                              *value + "'");
        }
        auto number = entry.numberIn(0, 255);
        if (!number) return tl::make_unexpected(std::move(number).error());
        palette.*(field->second) = Color{static_cast<uint8_t>(*number)};
    }
    return palette;
}

}  // namespace

term::Element selectionBold(term::Element child) {
    if (!palette.selectionBold) return child;
    return term::bold(std::move(child));
}

int approximatedRoles(const Palette& palette, int available) {
    // Every number a theme can hold, and a direct-color terminal reports a whole
    // 24-bit range here. Nothing is out of reach at either.
    if (available >= 256) return 0;

    int count = 0;
    for (const auto& role : kFields) {
        const Color color = palette.*(role.second);
        // A role left as the terminal's own color asks for no palette entry, so
        // there is no entry to fall short of — see term::Color.
        if (color.defaulted) continue;
        // A truecolor role. Everything above returned already where the terminal
        // has the whole palette, so what is left is a terminal with fewer than
        // 256 entries — one that can neither take the triple as written nor be
        // lent an entry out of the 256-color range to hold it. On a terminal
        // that has them this counts for nothing, which is why a truecolor theme
        // says nothing on the usual one: whether the entry lent to it is really
        // redrawn is not something a terminal can be asked.
        if (color.trueColor) {
            ++count;
            continue;
        }
        // The sixteen ANSI colors are the ones every terminal with color at all
        // has, and what it draws them as is its own configuration rather than
        // anything a number here settles. A theme written inside them is a theme
        // written for the terminal it is on, so nothing about it is worth saying.
        if (color.index() < 16) continue;
        if (color.index() >= available) ++count;
    }
    return count;
}

std::vector<uint8_t> ownEntries(const Palette& palette) {
    std::vector<uint8_t> used;
    for (const auto& role : kFields) {
        const Color color = palette.*(role.second);
        if (color.defaulted || color.trueColor) continue;
        used.push_back(color.index());
    }
    return used;
}

std::string colorWarning() {
    const int available = term::paletteSize();

    // One gate for both lines below: a theme that names nothing above the
    // sixteen ANSI colors is a theme this terminal draws as its own
    // configuration says to, and there is nothing to tell the user about it.
    if (approximatedRoles(palette, available) == 0) return {};

    // No color at all, which `has_colors()` answered before any of the rest was
    // asked. A line of its own because the one below quotes how many colors the
    // terminal has, and "only 0 colors" is not what to say to somebody looking
    // at an interface drawn in no colors whatever.
    if (available <= 0) {
        return _(
            "this terminal reports no color at all, so the interface is drawn in "
            "whatever two colors the terminal itself uses. If it does have color "
            "after all, color_warning off in the config disables this message.");
    }

    return i18n::format(
        _("colors may be distorted: this terminal can show only {0} colors, which "
          "is not enough for the current theme. Setting the TERM environment "
          "variable to xterm-256color usually fixes it, or you can use "
          "themes/16_colors.cfg, which uses only the system colors. If the colors "
          "look right, color_warning off in the config disables this message."),
        {std::to_string(available)});
}

tl::expected<Palette, ErrorPtr> parsePalette(const std::string& text,
                                             const std::string& originName) {
    auto entries = config::parseCfg(text, originName);
    if (!entries) return tl::make_unexpected(std::move(entries).error());
    return fromEntries(*entries);
}

tl::expected<Palette, ErrorPtr> loadPalette(const std::string& path) {
    const auto text = config::text::readFile(path);
    // Named as a theme rather than as a file: it is the config's `theme` line
    // that sent us here, and that is where the answer is.
    if (!text)
        return failure("cannot read theme " + path + ": " + text.error()->message());
    return parsePalette(*text, path);
}

}  // namespace amberedit::ui::theme
