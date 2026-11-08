#include "ui/output_dialog.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "i18n/i18n.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/scrollbar.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::output_dialog {

using namespace term;

namespace {

/// How wide the box stands inside its frame at most: the eighty columns a
/// program writing to a terminal writes against, less the frame. A narrower
/// window narrows it and the lines wrap.
constexpr int kInnerWidth = 78;
constexpr int kMinInner = 20;
constexpr int kFrame = 2;
/// The title rule, the blank row over the button and the bottom rule — the
/// button's own rows are counted beside these.
constexpr int kChromeRows = 3;
/// The rows it keeps clear of the top and bottom of the window.
constexpr int kWindowMargin = 2;

/// Lays the lines out against the window, once — and again only where the
/// window has changed size.
///
/// Wrapped a column narrower where they do not all fit, the column being the
/// scrollbar's: the reader's own two passes, and they end for the same reason —
/// wrapping narrower only ever makes the rows more.
void fitBox(const AppState& state, AppState::OutputView& view) {
    if (view.layoutWidth == state.width && view.layoutHeight == state.height) return;
    view.layoutWidth = state.width;
    view.layoutHeight = state.height;
    view.inner = std::min(kInnerWidth, std::max(kMinInner, state.width - kFrame));
    const int room = std::max(1, state.height - kChromeRows - kWindowMargin -
                                     dialog::buttonRows(state.dialogTallButtons()));

    const auto wrap = [&view](int width) {
        view.rows.clear();
        for (const std::string& line : view.lines) {
            std::vector<std::string> pieces = wrapText(line, width);
            if (pieces.empty()) pieces.emplace_back();
            for (std::string& piece : pieces) view.rows.push_back(std::move(piece));
        }
    };
    wrap(view.inner);
    view.scrollbar = static_cast<int>(view.rows.size()) > room;
    if (view.scrollbar) wrap(std::max(1, view.inner - 1));
    view.visible = std::max(1, std::min(static_cast<int>(view.rows.size()), room));
}

void close(AppState& state) {
    state.outputView.reset();
}

}  // namespace

void open(AppState& state, std::string title, std::vector<std::string> lines, bool cut) {
    AppState::OutputView view;
    view.title = std::move(title);
    view.lines = std::move(lines);
    view.cut = cut;
    if (view.lines.empty()) view.lines.emplace_back(_("(the program wrote nothing)"));
    state.outputView = std::move(view);
}

Element render(AppState& state, Element background) {
    AppState::OutputView& view = *state.outputView;
    fitBox(state, view);

    const int inner = view.inner;
    const auto total = static_cast<int>(view.rows.size());
    view.scroll = std::clamp(view.scroll, 0, std::max(0, total - view.visible));
    const int textWidth = view.scrollbar ? std::max(1, inner - 1) : inner;
    const scrollbar::Thumb thumb = scrollbar::thumbOf(view.visible, total, view.scroll);

    Elements lines{dialog::titleBar(" " + view.title + " ", inner)};
    for (int i = 0; i < view.visible; ++i) {
        const int at = view.scroll + i;
        const std::string content =
            at < total ? truncateToWidth(view.rows[static_cast<size_t>(at)], textWidth)
                       : std::string{};
        Element row =
            text(padRight(content, textWidth)) | color(theme::palette.dialogText);
        lines.push_back(view.scrollbar ? dialog::framed(hbox(
                                             {std::move(row), scrollbar::cell(i, thumb)}))
                                       : dialog::framed(std::move(row)));
    }
    lines.push_back(dialog::framed(text(std::string(static_cast<size_t>(inner), ' '))));

    // Centred by measuring, as the export box's Save is: a `filler()` would take
    // a share of a row nothing else on it is laid out against.
    const bool tall = state.dialogTallButtons();
    const int buttonRows = dialog::buttonRows(tall);
    const std::string okLabel = _("OK");
    const int buttonWidth = std::min(inner, dialog::buttonWidth(okLabel, tall));
    const int left = (inner - buttonWidth) / 2;
    const int right = inner - buttonWidth - left;
    Element ok = dialog::button(okLabel, /*selected=*/true,
                                state.isPressed(AppState::Pressed::OutputOk), tall) |
                 reflect(view.okBox);
    lines.push_back(dialog::framed(
        hbox({text(std::string(static_cast<size_t>(left), ' ')), std::move(ok),
              text(std::string(static_cast<size_t>(right), ' '))}),
        buttonRows));

    const std::string hint =
        view.cut ? _("only the first megabyte is shown") : std::string(_("Enter · Esc"));
    lines.push_back(dialog::bottomBar(hint, {}, inner));

    return dbox({std::move(background),
                 dialog::surface(vbox(std::move(lines)) | reflect(view.box)) | center});
}

void handleEvent(AppState& state, const Event& event) {
    AppState::OutputView& view = *state.outputView;
    const auto total = static_cast<int>(view.rows.size());
    const int last = std::max(0, total - view.visible);
    const auto scrollBy = [&view, last](int delta) {
        view.scroll = std::clamp(view.scroll + delta, 0, last);
    };

    if (const auto click = leftClick(event)) {
        if (view.okBox.Contain(click->x, click->y)) {
            state.showClick(AppState::Pressed::OutputOk);
            close(state);
        } else if (!view.box.Contain(click->x, click->y)) {
            close(state);
        }
        // Inside the box and off the button: the text, which there is nothing
        // to do with but read.
        return;
    }
    if (const int wheel = wheelDelta(event); wheel != 0) {
        scrollBy(wheel);
        return;
    }
    if (event == Event::ArrowDown) {
        scrollBy(1);
    } else if (event == Event::ArrowUp) {
        scrollBy(-1);
    } else if (event == Event::PageDown || event == Event::Character(' ')) {
        scrollBy(view.visible);
    } else if (event == Event::PageUp) {
        scrollBy(-view.visible);
    } else if (event == Event::Home) {
        view.scroll = 0;
    } else if (event == Event::End) {
        view.scroll = last;
    } else if (event == Event::Return || event == Event::Escape ||
               event == Event::Backspace || event == Event::Character('o') ||
               event == Event::Character('O')) {
        close(state);
    }
}

}  // namespace amberedit::ui::output_dialog
