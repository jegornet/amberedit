#include "ui/edit_history.hpp"

#include <utility>

namespace amberedit::ui {
namespace {

bool isBlank(std::string_view typed) {
    return typed == " " || typed == "\t";
}

/// Replaces `count` lines from `row` with `with`.
void splice(std::vector<std::string>& lines, int row, size_t count,
            const std::vector<std::string>& with) {
    const auto at = lines.begin() + row;
    lines.erase(at, at + static_cast<std::ptrdiff_t>(count));
    lines.insert(lines.begin() + row, with.begin(), with.end());
}

}  // namespace

void EditHistory::settle(const std::vector<std::string>& lines,
                         const std::vector<std::string>& header, const EditFocus& focus,
                         Kind kind, std::string_view typed) {
    if (!seen_) {
        seen_ = EditContent{lines, header, focus};
        return;
    }
    EditContent& seen = *seen_;

    // The run of lines that differs: everything the two have in common at the
    // front and at the back is left out, which on a keystroke leaves one line.
    const size_t before = seen.lines.size();
    const size_t after = lines.size();
    size_t front = 0;
    while (front < before && front < after && seen.lines[front] == lines[front]) {
        ++front;
    }
    size_t back = 0;
    while (back < before - front && back < after - front &&
           seen.lines[before - 1 - back] == lines[after - 1 - back]) {
        ++back;
    }
    const bool textChanged = before != after || front != before;
    const bool headerChanged = seen.header != header;
    if (!textChanged && !headerChanged) {
        seen.focus = focus;
        return;
    }

    Step step;
    step.kind = kind;
    step.before = seen.focus;
    step.after = focus;
    if (textChanged) {
        step.row = static_cast<int>(front);
        const auto from = static_cast<std::ptrdiff_t>(front);
        step.removed.assign(seen.lines.begin() + from,
                            seen.lines.end() - static_cast<std::ptrdiff_t>(back));
        step.inserted.assign(lines.begin() + from,
                             lines.end() - static_cast<std::ptrdiff_t>(back));
        splice(seen.lines, step.row, step.removed.size(), step.inserted);
    }
    if (headerChanged) {
        step.headerBefore = seen.header;
        step.headerAfter = header;
        seen.header = header;
    }
    seen.focus = focus;
    undone_.clear();

    if (continues(step, typed)) {
        Step& last = done_.back();
        last.inserted = std::move(step.inserted);
        if (headerChanged) last.headerAfter = std::move(step.headerAfter);
        last.after = step.after;
    } else {
        done_.push_back(std::move(step));
        if (done_.size() > kMaxSteps) done_.erase(done_.begin());
    }
    if (kind == Kind::Typing) typedBlank_ = isBlank(typed);
    sealed_ = false;
}

bool EditHistory::continues(const Step& step, std::string_view typed) const {
    if (sealed_ || done_.empty() || step.kind == Kind::Other) return false;
    const Step& last = done_.back();
    if (last.kind != step.kind || last.after != step.before) return false;
    // A blank typed after a word is where the next step begins: undoing takes
    // a sentence back a word at a time rather than all at once.
    if (step.kind == Kind::Typing && isBlank(typed) && !typedBlank_) return false;
    const bool lastInHeader = !last.headerAfter.empty();
    const bool stepInHeader = !step.headerAfter.empty();
    if (lastInHeader != stepInHeader) return false;
    // The one field — `after` matching `before` has said so — or the one
    // line, and nothing else: a keystroke that wrapped a quote or joined two
    // lines is a step of its own.
    if (stepInHeader) return last.inserted.empty() && step.inserted.empty() &&
                             last.removed.empty() && step.removed.empty();
    return last.row == step.row && last.removed.size() == 1 &&
           last.inserted.size() == 1 && step.removed.size() == 1 &&
           step.inserted.size() == 1;
}

void EditHistory::apply(const Step& step, bool forward, EditContent& content) {
    const auto& out = forward ? step.removed : step.inserted;
    const auto& in = forward ? step.inserted : step.removed;
    if (!out.empty() || !in.empty()) splice(content.lines, step.row, out.size(), in);
    const auto& header = forward ? step.headerAfter : step.headerBefore;
    if (!header.empty()) content.header = header;
    content.focus = forward ? step.after : step.before;
}

bool EditHistory::undo(EditContent& content) {
    if (done_.empty()) return false;
    Step step = std::move(done_.back());
    done_.pop_back();
    apply(step, false, content);
    seen_ = content;
    undone_.push_back(std::move(step));
    sealed_ = true;
    return true;
}

bool EditHistory::redo(EditContent& content) {
    if (undone_.empty()) return false;
    Step step = std::move(undone_.back());
    undone_.pop_back();
    apply(step, true, content);
    seen_ = content;
    done_.push_back(std::move(step));
    sealed_ = true;
    return true;
}

void EditHistory::clear() {
    seen_.reset();
    done_.clear();
    undone_.clear();
    typedBlank_ = false;
    sealed_ = false;
}

}  // namespace amberedit::ui
