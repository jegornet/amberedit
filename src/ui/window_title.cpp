#include "ui/window_title.hpp"

#include "app/navigator.hpp"
#include "i18n/i18n.hpp"

namespace amberedit::ui {

std::string windowTitle(const AppState& state) {
    switch (state.navigator.current()) {
        case app::ScreenId::AreaList: return _("Areas");
        case app::ScreenId::MessageList:
            return i18n::format(_("{0}: messages"), {state.currentArea.tag});
        case app::ScreenId::MessageRead:
            return i18n::format(_("{0}: reading"), {state.currentArea.tag});
        case app::ScreenId::Compose:
            // The area the message goes into, which a moved reply or a
            // forward makes another area than the one being read.
            return i18n::format(_("{0}: composing"), {state.composeArea().tag});
    }
    return {};
}

}  // namespace amberedit::ui
