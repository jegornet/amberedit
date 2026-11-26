#include "ui/window_title.hpp"

#include "app/navigator.hpp"
#include "i18n/i18n.hpp"

namespace amberedit::ui {

std::string windowTitle(const AppState& state) {
    switch (state.navigator.current()) {
        case app::ScreenId::AreaList: return _("Areas");
        case app::ScreenId::MessageList:
            return i18n::format(_("Messages in {0}"), {state.currentArea.tag});
        case app::ScreenId::MessageRead:
            return i18n::format(_("Reading {0}"), {state.currentArea.tag});
        case app::ScreenId::Compose:
            // The area the message goes into, which a moved reply or a
            // forward makes another area than the one being read.
            return i18n::format(_("Writing to {0}"), {state.composeArea().tag});
    }
    return {};
}

}  // namespace amberedit::ui
