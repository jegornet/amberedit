#pragma once

#include <cstdint>

#include "app/navigator.hpp"
#include "ui/app_state.hpp"

namespace amberedit::ui {

/// What an event arriving now would be handed: the box in front of the user, or
/// the screen itself where there is none.
///
/// The order is the dispatch chain's in `app_shell.cpp` and the two are to be
/// kept in step — a box left out of here is one the tail of a flick of the wheel
/// can still scroll, which is all that hangs on it. Nothing else reads this:
/// what a key means is decided by that chain and not by this enum.
enum class Addressee : uint8_t {
    Screen,
    Attributes,
    Error,
    Confirm,
    Menu,
    Forward,
    Area,
    Import,
    External,
    ExportMode,
    Export,
    Find,
    Nodelist,
    Info,
    Replies,
};

[[nodiscard]] inline Addressee addresseeOf(const AppState& state) {
    if (state.attributePicker) return Addressee::Attributes;
    if (!state.errorMessage.empty()) return Addressee::Error;
    if (state.confirm != AppState::Confirm::None) return Addressee::Confirm;
    if (state.menuView) return Addressee::Menu;
    if (state.forwardPicker) return Addressee::Forward;
    if (state.areaPicker) return Addressee::Area;
    if (state.importPicker) return Addressee::Import;
    if (state.externalReview) return Addressee::External;
    if (state.exportModePicker) return Addressee::ExportMode;
    if (state.exportPicker) return Addressee::Export;
    if (state.findPicker) return Addressee::Find;
    if (state.nodelistView) return Addressee::Nodelist;
    if (state.infoView) return Addressee::Info;
    if (!state.replyChoices.empty()) return Addressee::Replies;
    return Addressee::Screen;
}

/// What is in front of the user, as the wheel cares about it. Three questions
/// rather than one, because each of them changes without the others: a box is
/// put away by the same key that opens the next screen — the reply list picks a
/// message and the reader goes to it — and the reader walks from one message to
/// the next without either the screen or the box over it changing at all.
///
/// The message is the one the reader has loaded and not the list's cursor, which
/// the two share. That matters: the wheel moves that cursor a row at a time, and
/// a cursor in here would make every notch over the message list a change of
/// what is in front of the user, which is the one thing this must never say.
struct Focus {
    app::ScreenId screen{app::ScreenId::AreaList};
    Addressee addressee{Addressee::Screen};
    uint32_t message{0};

    [[nodiscard]] bool operator==(const Focus& other) const {
        return screen == other.screen && addressee == other.addressee &&
               message == other.message;
    }
    [[nodiscard]] bool operator!=(const Focus& other) const { return !(*this == other); }
};

[[nodiscard]] inline Focus focusOf(const AppState& state) {
    return {state.navigator.current(), addresseeOf(state),
            state.readHeader ? state.readHeader->number : 0};
}

}  // namespace amberedit::ui
