#include "ui/dialog_field.hpp"

#include <optional>
#include <string>
#include <utility>

namespace amberedit::ui::dialog_field {

FieldContext contextOf(AppState& state, size_t limit) {
    FieldContext context{state.keys};
    context.now = state.monotonicMs();
    context.copyOnSelect = state.config.clipboardCopyOnSelect;
    context.limit = limit;
    context.copy = [&state](std::string text, bool quiet) {
        if (state.config.clipboardInEffect() == config::ClipboardMode::Internal) {
            state.clipboard = std::move(text);
            state.copiedNotice = state.config.clipboardCopyIndicate && !quiet;
        } else {
            state.clipboardCopyRequested = std::move(text);
            state.clipboardCopyQuiet = quiet;
        }
    };
    context.paste = [&state]() -> std::optional<std::string> {
        if (state.config.clipboardInEffect() == config::ClipboardMode::Internal) {
            return state.clipboard;
        }
        state.clipboardPasteRequested = true;
        return std::nullopt;
    };
    return context;
}

TextField* focused(AppState& state) {
    if (state.findPicker) {
        auto& picker = *state.findPicker;
        return picker.focus == AppState::FindPicker::Focus::Query ? &picker.query
                                                                  : nullptr;
    }
    if (state.charsetPicker) {
        auto& picker = *state.charsetPicker;
        return picker.focus == AppState::CharsetPicker::Focus::Charset ? &picker.charset
                                                                       : nullptr;
    }
    if (state.importPicker) {
        auto& picker = *state.importPicker;
        return picker.focus == AppState::ImportPicker::Focus::Path ? &picker.path
                                                                   : nullptr;
    }
    if (state.exportPicker) {
        auto& picker = *state.exportPicker;
        if (picker.existing) return nullptr;
        if (picker.focus == AppState::ExportPicker::Focus::Path) return &picker.path;
        if (picker.focus == AppState::ExportPicker::Focus::Name &&
            picker.mode == AppState::ExportPicker::Mode::Text) {
            return &picker.name;
        }
    }
    return nullptr;
}

void paste(AppState& state, std::string_view text) {
    if (TextField* field = focused(state)) pasteIntoField(*field, text);
}

}  // namespace amberedit::ui::dialog_field
