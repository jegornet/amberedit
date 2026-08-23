#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "ui/dir_listing.hpp"
#include "ui/term/box.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/text_field.hpp"

namespace amberedit::ui::setup {

/// The directory browser the wizard picks a file with: a path box that is typed
/// into, a listing walked with the arrows and searched by typing, and `..` at
/// the top of it.
///
/// The import dialog's habits without the import dialog: that one reaches into
/// `AppState` for the directory it is showing and for the file it read, and
/// there is no `AppState` before there is a config. What it gains is a filter,
/// since two of the three tosser formats keep their config under one name and a
/// listing that showed everything would be asking the user to find it.
struct FilePicker {
    /// Which files are shown. Directories are always shown — a file is picked by
    /// walking to it. Left unset, everything is.
    std::function<bool(std::string_view name)> accepts;
    /// The directory the listing is of.
    std::string directory;
    std::vector<DirEntry> entries;
    int cursor{0};
    int offset{0};
    /// The quick search, as far as it has been typed.
    std::string search;
    TextField path;
    /// Whether the typing goes to the path box or to the listing.
    bool onPath{false};
    /// The file that has been picked, empty until one is.
    std::string chosen;
    /// What the last thing tried is answered with, drawn by the wizard in the
    /// bottom rule of its own frame.
    std::string error;
    /// How many rows the listing has room for, settled by the wizard.
    int rows{1};

    struct Row {
        int index{0};
        term::Box box{term::Box::Nowhere()};
    };
    std::vector<Row> rowBoxes;
};

/// What an event did to the picker.
enum class PickOutcome {
    /// Nothing the wizard has to know about.
    Ignored,
    /// A file was picked, and `chosen` says which.
    Picked,
    /// The event was not the picker's — the wizard's own ring should have it.
    Unclaimed,
};

/// Opens the picker on a directory, or on the working directory where that one
/// will not open.
void open(FilePicker& picker, const std::string& directory);

/// The file the cursor stands on, empty where it stands on a directory or the
/// listing has nothing in it.
///
/// The row under the cursor is the row that is lit, and a lit row is what a user
/// has said they mean — by clicking it or by walking to it. So the step asks
/// this when it is left, and neither a click that has not been repeated nor an
/// arrow that was not followed by Enter is an answer the wizard throws away.
[[nodiscard]] std::string fileUnderCursor(const FilePicker& picker);

/// The rows of the picker: the path box, a rule, and the listing.
[[nodiscard]] term::Element render(FilePicker& picker, int inner);

/// A click inside the picker. Answers Unclaimed where it landed outside it,
/// which is how the wizard's own buttons get theirs.
PickOutcome handleClick(FilePicker& picker, const term::MouseEvent& click);

/// A key in the picker. Tab and Escape are never claimed — they belong to the
/// wizard around it — and neither is Enter on a file, which is answered with
/// Picked so that the wizard decides what picking one means.
PickOutcome handleEvent(FilePicker& picker, const term::Event& event);

}  // namespace amberedit::ui::setup
