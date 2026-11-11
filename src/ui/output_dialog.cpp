#include "ui/output_dialog.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "i18n/i18n.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/keys.hpp"
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

/// Puts the output on the clipboard, the way every copy does: AmberEdit's own
/// answered on the spot, any other left for the shell, which has the terminal
/// and the programs. Nothing at all where the program wrote nothing.
void copy(AppState& state) {
    std::string text = copiedText(*state.outputView);
    if (text.empty()) return;
    if (state.config.clipboardInEffect() == config::ClipboardMode::Internal) {
        state.clipboard = std::move(text);
        state.copiedNotice = state.config.clipboardCopyIndicate;
    } else {
        state.clipboardCopyRequested = std::move(text);
    }
}

/// The key the box copies on, from anywhere in it: Ctrl-C whatever the layout
/// says, and whatever keys the layout gives copying in the reader and the
/// editor besides — Ctrl-Ins among them — so that the hand reaches for what it
/// reaches for everywhere else.
bool isCopyKey(const AppState& state, const Event& event) {
    return isCtrl(event, 'c') || state.keys.is(event, Command::ReaderCopy) ||
           state.keys.is(event, Command::ComposeCopy);
}

}  // namespace

void open(AppState& state, std::string title, std::vector<std::string> lines, bool cut) {
    AppState::OutputView view;
    view.title = std::move(title);
    view.lines = std::move(lines);
    view.cut = cut;
    if (view.lines.empty()) {
        view.empty = true;
        view.lines.emplace_back(_("(the program wrote nothing)"));
    }
    state.outputView = std::move(view);
}

std::string copiedText(const AppState::OutputView& view) {
    if (view.empty) return {};
    std::string text;
    for (size_t i = 0; i < view.lines.size(); ++i) {
        if (i != 0) text += '\n';
        text += view.lines[i];
    }
    return text;
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
        lines.push_back(
            view.scrollbar
                ? dialog::framed(hbox(
                      {std::move(row), scrollbar::cell(i, thumb, scrollbar::On::Dialog)}))
                : dialog::framed(std::move(row)));
    }
    lines.push_back(dialog::framed(text(std::string(static_cast<size_t>(inner), ' '))));

    // Centred by measuring, as the export box's Save is: a `filler()` would take
    // a share of a row nothing else on it is laid out against.
    const bool tall = state.dialogTallButtons();
    const int buttonRows = dialog::buttonRows(tall);
    const std::string okLabel = _("OK");
    const std::string copyLabel = _("Copy");
    constexpr int kButtonGap = 2;
    const int buttonsWidth =
        std::min(inner, dialog::buttonWidth(okLabel, tall) + kButtonGap +
                            dialog::buttonWidth(copyLabel, tall));
    const int left = (inner - buttonsWidth) / 2;
    const int right = inner - buttonsWidth - left;
    Element ok = dialog::button(okLabel, !view.copySelected,
                                state.isPressed(AppState::Pressed::OutputOk), tall) |
                 reflect(view.okBox);
    Element copyButton =
        dialog::button(copyLabel, view.copySelected,
                       state.isPressed(AppState::Pressed::OutputCopy), tall) |
        reflect(view.copyBox);
    // Copy first and OK last, where the button that ends the box stands in
    // every other box — and OK still the one Enter is on as it opens.
    lines.push_back(dialog::framed(
        hbox({text(std::string(static_cast<size_t>(left), ' ')), std::move(copyButton),
              text(std::string(kButtonGap, ' ')), std::move(ok),
              text(std::string(static_cast<size_t>(right), ' '))}),
        buttonRows));

    const std::string hint = view.cut ? _("only the first megabyte is shown")
                                      : std::string(_("Ctrl-C copies · Esc closes"));
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
        } else if (view.copyBox.Contain(click->x, click->y)) {
            view.copySelected = true;
            state.showClick(AppState::Pressed::OutputCopy);
            copy(state);
        } else if (!view.box.Contain(click->x, click->y)) {
            close(state);
        }
        // Inside the box and off the buttons: the text, which there is nothing
        // to do with but read.
        return;
    }
    if (const int wheel = wheelDelta(event); wheel != 0) {
        scrollBy(wheel);
        return;
    }
    if (isCopyKey(state, event)) {
        copy(state);
        return;
    }
    if (event == Event::ArrowLeft || event == Event::ArrowRight || event == Event::Tab ||
        event == Event::TabReverse) {
        view.copySelected = !view.copySelected;
        return;
    }
    if (event == Event::Return) {
        if (view.copySelected) {
            copy(state);
        } else {
            close(state);
        }
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
    } else if (event == Event::Escape || event == Event::Backspace ||
               event == Event::Character('o') || event == Event::Character('O')) {
        close(state);
    }
}

}  // namespace amberedit::ui::output_dialog
