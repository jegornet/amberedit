#include "ui/window_title.hpp"

#include "app/navigator.hpp"
#include "i18n/i18n.hpp"
#include "version.hpp"

namespace amberedit::ui {

std::string windowTitle(const AppState& state) {
    std::string screen;
    switch (state.navigator.current()) {
        case app::ScreenId::AreaList: screen = _("Areas"); break;
        case app::ScreenId::MessageList:
            screen = i18n::format(_("Messages in {0}"), {state.currentArea.tag});
            break;
        case app::ScreenId::MessageRead:
            screen = state.currentArea.tag;
            break;
        case app::ScreenId::Compose:
            // The area the message goes into, which a moved reply or a
            // forward makes another area than the one being read.
            screen = i18n::format(_("Writing to {0}"), {state.composeArea().tag});
            break;
    }
    return screen + " — " + std::string(kProgramName);
}

}  // namespace amberedit::ui
