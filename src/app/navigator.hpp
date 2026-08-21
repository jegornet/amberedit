#pragma once

#include <cstddef>
#include <vector>

namespace amberedit::app {

/// The MVP's screens.
enum class ScreenId {
    AreaList,     ///< list of areas
    MessageList,  ///< list of messages in the selected area
    MessageRead,  ///< the current message
    Compose,      ///< a message being written, header block and text as one
};

/// A stack of screens. It knows about transitions and nothing else — no data,
/// nothing the terminal owns — so navigation logic stays testable without one.
class Navigator {
public:
    Navigator() : stack_{ScreenId::AreaList} {}

    [[nodiscard]] ScreenId current() const { return stack_.back(); }
    [[nodiscard]] size_t depth() const { return stack_.size(); }

    void push(ScreenId screen) { stack_.push_back(screen); }

    /// Goes back one screen. false means we are on the root screen (the area
    /// list) with nowhere to go back to; callers read that as "quit".
    bool pop() {
        if (stack_.size() <= 1) return false;
        stack_.pop_back();
        return true;
    }

    void reset() { stack_.assign(1, ScreenId::AreaList); }

private:
    std::vector<ScreenId> stack_;
};

}  // namespace amberedit::app
