#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ui/text_editor.hpp"

namespace amberedit::ui {

/// Where the typing was: which half of the compose screen, which field and
/// where in it, and where in the text. Undoing a change puts the typing back
/// where it stood when the change was made, which is what tells the user what
/// was just undone.
struct EditFocus {
    bool inHeader{false};
    int field{0};
    size_t fieldCursor{0};
    TextPoint text;

    [[nodiscard]] bool operator==(const EditFocus& other) const {
        return inHeader == other.inHeader && field == other.field &&
               fieldCursor == other.fieldCursor && text == other.text;
    }
    [[nodiscard]] bool operator!=(const EditFocus& other) const {
        return !(*this == other);
    }
};

/// What the message looks like to the history: its lines, the header fields
/// typed into, and where the typing stands.
struct EditContent {
    std::vector<std::string> lines;
    std::vector<std::string> header;
    EditFocus focus;
};

/// The undo and redo of the message being written.
///
/// **It watches rather than being told.** `settle()` is handed the message as
/// it stands after each keystroke and compares it with what it saw last; what
/// differs is a step. So every way the text can change — a key, a paste the
/// shell answered, a file imported, a utility handing the message back — is
/// undoable without any of them knowing there is a history, and a new command
/// that edits the message is undoable the day it is written.
///
/// A step keeps the lines that changed and nothing else: the run between the
/// first line that differs and the last, before and after. A message of a
/// thousand lines typed into a character at a time costs a line a step, not a
/// message a step.
///
/// **Typing is one step a word at a time**, as every editor does it: the
/// characters typed in a run go into the step before them, a space after a word
/// beginning the next one, and so do Backspace and Delete pressed in a run.
/// Anything else between two keystrokes — the cursor moved, a line deleted, the
/// typing gone to another field — ends the run.
class EditHistory {
public:
    /// What a keystroke was, for gathering a run of them into one step.
    enum class Kind { Typing, Deleting, Other };

    /// Records what has changed since the last call as a step of `kind`,
    /// `typed` being what a `Typing` step typed. The first call after
    /// `clear()` records nothing: what it is handed is where the history
    /// begins. A change throws away whatever was undone and not redone.
    ///
    /// Handed the parts rather than an `EditContent`, so that a keystroke costs
    /// a comparison of the message and not a copy of it.
    void settle(const std::vector<std::string>& lines,
                const std::vector<std::string>& header, const EditFocus& focus, Kind kind,
                std::string_view typed = {});

    /// Takes the last step back, or puts the last step undone back again, into
    /// `content` — which is the message as it stands, and has only what the
    /// step touched changed in it. Says whether there was a step to take.
    bool undo(EditContent& content);
    bool redo(EditContent& content);

    /// Forgets everything, the next `settle()` beginning afresh.
    void clear();

private:
    struct Step {
        /// The first line that differs, and the lines from there that the
        /// change took out and put in.
        int row{0};
        std::vector<std::string> removed;
        std::vector<std::string> inserted;
        /// The header fields before and after, where they changed; both empty
        /// where they did not.
        std::vector<std::string> headerBefore;
        std::vector<std::string> headerAfter;
        EditFocus before;
        EditFocus after;
        Kind kind{Kind::Other};
    };

    /// Whether `step` carries on the run the last step is: a keystroke of the
    /// same kind, made where the last one left the typing, on the one line or
    /// the one field.
    [[nodiscard]] bool continues(const Step& step, std::string_view typed) const;

    /// Moves a step one way or the other, into `content` and into what was
    /// last seen alike.
    static void apply(const Step& step, bool forward, EditContent& content);

    /// How many steps are kept, the oldest going first. Far more than anybody
    /// undoes, and a bound on what a message pasted into all afternoon holds.
    static constexpr size_t kMaxSteps = 1000;

    std::optional<EditContent> seen_;
    std::vector<Step> done_;
    std::vector<Step> undone_;
    /// Whether the last character a run typed was a blank, which is where a
    /// word ends and the next character typed begins a step of its own.
    bool typedBlank_{false};
    /// Set by an undo or a redo: what is typed next is a step of its own, and
    /// not more of whichever step happens to stand at the top.
    bool sealed_{false};
};

}  // namespace amberedit::ui
