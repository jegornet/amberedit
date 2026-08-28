#include "ui/app_shell.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "app/external_editor.hpp"
#include "app/run_program.hpp"
#include "app/url_handler.hpp"
#include "app/user_shell.hpp"
#include "i18n/i18n.hpp"
#include "ui/after_handover.hpp"
#include "ui/app_state.hpp"
#include "ui/area_dialog.hpp"
#include "ui/attributes_dialog.hpp"
#include "ui/confirm_dialog.hpp"
#include "ui/error_dialog.hpp"
#include "ui/error_log.hpp"
#include "ui/export_dialog.hpp"
#include "ui/export_mode_dialog.hpp"
#include "ui/external_dialog.hpp"
#include "ui/find_dialog.hpp"
#include "ui/focus.hpp"
#include "ui/forward_dialog.hpp"
#include "ui/hint_bar.hpp"
#include "ui/import_dialog.hpp"
#include "ui/info_dialog.hpp"
#include "ui/mark_dialog.hpp"
#include "ui/menu_dialog.hpp"
#include "ui/nodelist_dialog.hpp"
#include "ui/reply_dialog.hpp"
#include "ui/rescan_dialog.hpp"
#include "ui/scope_dialog.hpp"
#include "ui/screens/area_list_screen.hpp"
#include "ui/screens/compose_screen.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/terminal.hpp"
#include "ui/term/utf8.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui {

using namespace term;

namespace {

/// What the area dialog is being opened for, given the answer to the one before
/// it. The two enums are deliberately apart: the first dialog asks what is to
/// become of the message and knows nothing of areas, and the second one is asked
/// by `n` as well, which the first never sees.
AppState::AreaPicker::For purposeOf(AppState::ForwardPicker::Mode mode) {
    switch (mode) {
        case AppState::ForwardPicker::Mode::Move: return AppState::AreaPicker::For::Move;
        case AppState::ForwardPicker::Mode::Copy: return AppState::AreaPicker::For::Copy;
        case AppState::ForwardPicker::Mode::Forward: break;
    }
    return AppState::AreaPicker::For::Forward;
}

/// What the error log calls the screen that was up. The errors it keeps are
/// about a message base rather than about the interface, so which screen was in
/// front of the user is the shortest way to say what was being read at the time.
const char* screenName(app::ScreenId screen) {
    switch (screen) {
        case app::ScreenId::AreaList: return "area list";
        case app::ScreenId::MessageList: return "message list";
        case app::ScreenId::MessageRead: return "reader";
        case app::ScreenId::Compose: break;
    }
    return "compose";
}

/// What the log calls the event that was being answered. A keystroke is named
/// the way a `keys` file would name it; the pointer is not, its position being
/// about the frame it landed on rather than about anything a later reader of the
/// log can act on.
std::string eventName(const Event& event) {
    if (event.is_mouse()) return "mouse";
    return spellingOf(event);
}

/// The whole interface, as one tree of boxes, for whichever screen is up.
///
/// Rendering touches the message base — loading headers, re-wrapping a body — so
/// it can fail, and a base that cannot be read must not bring the application
/// down while the user is looking at it.
Element document(AppState& state) {
    Element body = text("");
    try {
        switch (state.navigator.current()) {
            case app::ScreenId::AreaList: body = screens::area_list::render(state); break;
            case app::ScreenId::MessageList:
                screens::message_list::ensureHeaders(state);
                body = screens::message_list::render(state);
                break;
            case app::ScreenId::MessageRead:
                screens::message_read::relayout(state);
                body = screens::message_read::render(state);
                break;
            case app::ScreenId::Compose: body = screens::compose::render(state); break;
        }
    } catch (const std::exception& e) {
        // On the screen in place of what would not draw, and in the log as well:
        // the row says something went wrong and the file is the only place that
        // still says so once the next frame has painted over it.
        error_log::write(screenName(state.navigator.current()),
                         "drawing the screen: " + std::string(e.what()));
        body = text(i18n::format(_(" error: {0}"), {e.what()})) |
               color(theme::palette.error);
    }

    // Thirteen modals, and only ever one of them at a time: the context menu is
    // the one thing the area list, the reader and the editor all open, the
    // confirmation is asked from screens neither of the two lists can be up on,
    // nine of them are the reader's, opened by different keys, two are the
    // editor's, and the error box comes up on the area list in place of the
    // screen that would not open.
    //
    // The menu is drawn first, under the rest: everything it can do is put a
    // box of its own on the screen, and it is gone by the time that box is up.
    if (state.menuView) {
        body = menu_dialog::render(state, std::move(body));
    }
    if (!state.replyChoices.empty()) {
        body = reply_dialog::render(state, std::move(body));
    }
    if (state.infoView) {
        body = info_dialog::render(state, std::move(body));
    }
    if (state.nodelistView) {
        body = nodelist_dialog::render(state, std::move(body));
    }
    // The two halves of `m`, one after the other: what is to become of the
    // message, and then which area it is to become that in.
    if (state.forwardPicker) {
        body = forward_dialog::render(state, std::move(body));
    }
    if (state.areaPicker) {
        body = area_dialog::render(state, std::move(body));
    }
    // Both over the compose screen, which is the one screen either opens from.
    if (state.attributePicker) {
        body = attributes_dialog::render(state, std::move(body));
    }
    if (state.importPicker) {
        body = import_dialog::render(state, std::move(body));
    }
    // And this one over the editor as well, where the message came back from a
    // program of the user's own: it asks what is to become of what that program
    // left, and the message stands behind it to be read.
    if (state.externalReview) {
        body = external_dialog::render(state, std::move(body));
    }
    // And these two over the reader, which is the one screen either opens from —
    // the second in the first's place, the two being one question in two halves
    // the way the forward picker and the area picker are.
    if (state.exportModePicker) {
        body = export_mode_dialog::render(state, std::move(body));
    }
    if (state.exportPicker) {
        body = export_dialog::render(state, std::move(body));
    }
    if (state.findPicker) {
        body = find_dialog::render(state, std::move(body));
    }
    if (state.markPicker) {
        body = mark_dialog::render(state, std::move(body));
    }
    if (state.scopePicker) {
        body = scope_dialog::render(state, std::move(body));
    }
    if (state.confirm != AppState::Confirm::None) {
        body = confirm_dialog::render(state, std::move(body));
    }
    if (!state.errorMessage.empty()) {
        body = error_dialog::render(state, std::move(body));
    }
    // Over whichever screen is up. Ctrl-R asks for one from the area list, and
    // `rescan_on_return` asks for one on the way back from a program that had
    // the terminal — which is the reader or the area list, the box standing over
    // the message being read as readily as over the list.
    if (state.rescanning) {
        body = rescan_dialog::render(state, std::move(body));
    }

    // The hint bar under the lot, dialogs included: it says what the screen
    // behind them does, and it is the one row `runApp()` has already taken off
    // their height for. It paints its own black over the fill below, which is
    // the last word on that row since a decorator paints before its children.
    if (state.hintBarShown()) {
        body = vbox({std::move(body) | flex, hint_bar::render(state)});
    }

    // The palette is painted across the whole screen rather than left to show
    // through: the theme's colors were chosen against its own background, and on
    // a light terminal the quiet greys would be unreadable.
    //
    // The text color goes on with it, and for the same reason. A cell drawn in
    // no color of its own — a plain column of either list — would otherwise keep
    // whatever foreground the terminal uses when nothing is asked for, which on
    // a profile like Terminal.app's own is black, and black on the theme's own
    // background is what nothing else on the screen is. A decorator paints
    // before its children, so every color asked for below still has the last
    // word.
    return std::move(body) | bgcolor(theme::palette.background) |
           color(theme::palette.text);
}

}  // namespace

int runApp(app::AreaManager& manager, const config::AppConfig& config,
           const KeyMap& keys) {
    AppState state(manager, config);
    state.keys = keys;
    // Before the terminal is taken over, so that anything the first frame throws
    // is already being kept. An empty path leaves the log off, which is what a
    // config stating no `error_log` asks for.
    error_log::open(config.errorLogPath);
    // The terminal is told which letters Alt is held with before it starts
    // reading any: a layout binding none leaves Escape unambiguous everywhere.
    Terminal terminal(keys.altLetters(), keys.altBackspace());

    // Putting a frame on the screen from inside whatever is running, rather
    // than at the top of the loop. It lives here because the terminal does, and
    // it is what lets a long call — the rescan naming each area as it opens it —
    // show its own progress while the loop is blocked in it.
    state.drawFrame = [&state, &terminal] { terminal.draw(document(state)); };

    // How a click is shown before it is acted on. The screens say when — the
    // button they have just been clicked on, or the row they have just moved
    // the cursor onto — and this draws that frame and leaves it there for
    // `click_animation_ms`. It is the whole of the animation: what a screen
    // does after asking for it is written as though there were none.
    //
    // The pause is a plain sleep rather than a deadline the poll loop watches.
    // Nothing is animated in the meantime and the click has already been read,
    // so there is nothing for the loop to do that waiting here would delay —
    // and a keystroke arriving mid-pause is not lost, only read a fifth of a
    // second later, ncurses having buffered it.
    state.holdFrame = [&state] {
        state.redraw();
        std::this_thread::sleep_for(std::chrono::milliseconds(state.clickAnimationMs));
    };

    // The window as it stands, which every screen lays itself out against. A
    // lambda rather than two lines at the top of the loop because the handovers
    // want it as well: the window may have been resized while another program
    // had the terminal, and what runs on the way back — the rescan's modal, a
    // message wrapped afresh — would otherwise be measured against the old one.
    const auto takeSize = [&state, &terminal] {
        state.width = terminal.width();
        state.height = terminal.height();
        // The hint bar's row comes off the height every screen lays itself out
        // against, here rather than in each of them: a screen has no business
        // knowing what stands under it, and the ones that draw no hints are a
        // row shorter all the same so that moving between them moves nothing.
        if (state.hintBarShown() && state.height > 1) --state.height;
    };

    // What the last frame was drawn from — the screen, the box over it, and the
    // message the reader has loaded. The wheel is the whole of the reason it is
    // remembered: a flick still arriving when Escape closes the reader was aimed
    // at the message it closed, and the list underneath is not to be run to its
    // end by it — see AppState::wheelLeftOver. It is read here rather than at
    // each place that opens or closes something, so that every way of putting
    // something else in front of the user is covered by the one test.
    Focus shown = focusOf(state);

    while (terminal.running()) {
        if (const Focus now = focusOf(state); now != shown) {
            shown = now;
            state.wheelFocusChanged();
        }

        takeSize();
        terminal.draw(document(state));

        // A rescan is asked for on one frame and done on the next. Opening every
        // base again is what takes the time, so the modal saying so has to be on
        // the screen — the frame just drawn — before it starts, and there is no
        // second thread for it to run on.
        //
        // Whatever was typed meanwhile is dropped rather than acted on when it
        // ends: those keys were aimed at counts that were being rebuilt, and a
        // letter into the quick search or an Enter opening an area would land on
        // a list nobody has looked at yet.
        if (state.rescanning) {
            screens::area_list::rescan(state);
            state.rescanning = false;
            terminal.flushInput();
            continue;
        }

        // The shell, for as long as the user is in it. Asked for on the frame
        // before, so that the menu the button was picked from is already off the
        // screen the shell is handed: what they see when their prompt scrolls up
        // is the reader, not a box over it.
        //
        // What went wrong is carried out rather than said inside: until handOver
        // has returned there is no screen to draw a box on. A shell that ran and
        // exited non-zero is not a failure — see app/user_shell.hpp.
        if (state.shellRequested) {
            state.shellRequested = false;
            std::string failed;
            terminal.handOver([&failed] {
                const auto ran = app::runUserShell();
                if (!ran) failed = ran.error()->message();
            });
            if (!failed.empty()) {
                state.errorMessage = failed;
                // The reader is still standing behind the box, and is where
                // acknowledging it leaves the user.
                state.errorEndsScreen = false;
            } else {
                // What the shell did to the base while it had the screen, which
                // is anything at all. Not where it never started: nothing can
                // have changed, and the refresh has a box of its own to raise
                // that would stand over the one just written above.
                takeSize();
                after_handover::refresh(state);
            }
            // Whatever was typed at the prompt after the shell had gone belongs
            // to the shell rather than to the screen coming back.
            terminal.flushInput();
            continue;
        }

        // An external utility, which is the shell with the program named in
        // advance: the same frame's wait, the same handover, and the same
        // silence about what it exited with. Which screen asked is nothing to
        // this — a slot is one utility however many commands reach it.
        if (state.externUtilRequested) {
            const size_t slot = *state.externUtilRequested;
            state.externUtilRequested.reset();
            std::string failed;
            terminal.handOver([&state, slot, &failed] {
                const auto ran = app::runProgram(state.config.externUtils[slot].command);
                if (!ran) failed = ran.error()->message();
            });
            if (!failed.empty()) {
                state.errorMessage = failed;
                // Whichever screen asked is still standing behind the box, and
                // is where acknowledging it leaves the user.
                state.errorEndsScreen = false;
            } else {
                // And the same on the way back, for the same reason: a utility
                // is a program that had the base to itself.
                takeSize();
                after_handover::refresh(state);
            }
            // Whatever was typed while the utility had the terminal was aimed
            // at it and not at the screen coming back.
            terminal.flushInput();
            continue;
        }

        // The editor the message is written in, where the config names one.
        // The same frame's wait and the same handover as the two above, and
        // one thing neither of them has: what the program left behind is the
        // message, so the file is written before it starts and read after it
        // ends — `app/external_editor.cpp` doing both, this having no business
        // with charsets.
        //
        // The base is not read again on the way back, exactly as it is not
        // after a utility run from this screen: `after_handover::refresh()`
        // leaves the editor alone on purpose — nothing on it comes off the
        // base, and reopening the area could drop the screen and the
        // half-written message with it.
        if (state.externalEditRequested) {
            state.externalEditRequested = false;
            std::string failed;
            // Made before the handover rather than inside it: a `tmpdir` that
            // will not take the file is a failure with nothing to hand over
            // for, and the box saying so wants the screen this still has.
            if (state.externalEditPath.empty()) {
                auto path = app::externalEditPath(state.config.tempDirPath);
                if (path) {
                    state.externalEditPath = *path;
                } else {
                    failed = path.error()->message();
                }
            }
            app::ExternalEdit edited;
            if (failed.empty()) {
                terminal.handOver([&state, &edited, &failed] {
                    // The terminal's own charset: the editor runs in this
                    // terminal, and a file it can show is one written the way
                    // this terminal reads one.
                    auto ran = app::runExternalEditor(
                        state.config.externalEditor, state.externalEditPath,
                        state.edit.lines, ensureUtf8Locale());
                    if (!ran) {
                        failed = ran.error()->message();
                    } else {
                        edited = std::move(*ran);
                    }
                });
            }
            takeSize();
            if (!failed.empty()) {
                state.errorMessage = failed;
                // The editor is still standing behind the box, with the message
                // untouched, and is where acknowledging it leaves the user.
                state.errorEndsScreen = false;
                screens::compose::externalEditFailed(state);
            } else {
                screens::compose::externalEditReturned(state, edited.changed,
                                                       std::move(edited.lines));
            }
            // Whatever was typed while the editor had the terminal was aimed at
            // it and not at the screen coming back.
            terminal.flushInput();
            continue;
        }

        // The program a link is opened with, the same way and for the same
        // reasons: asked for on the frame before, handed the terminal for as
        // long as it runs — a text browser wants the screen, and one that opens
        // a window elsewhere is done before the screen has been missed.
        if (!state.urlRequested.empty()) {
            const std::string url = state.urlRequested;
            state.urlRequested.clear();
            std::string failed;
            terminal.handOver([&state, &url, &failed] {
                const auto ran = app::runUrlHandler(state.config.urlHandler, url);
                if (!ran) failed = ran.error()->message();
            });
            if (!failed.empty()) {
                state.errorMessage = failed;
                // The reader is still standing behind the box, and is where
                // acknowledging it leaves the user.
                state.errorEndsScreen = false;
            }
            // Whatever was typed while the program had the terminal was aimed
            // at it and not at the message coming back.
            terminal.flushInput();
            continue;
        }

        Event event = terminal.poll();

        // What is left of a flick of the wheel that ended on the screen, the box
        // or the message before is answered by nothing at all: the notches were
        // aimed at what has since been put away, and they are dropped ahead of
        // every modal below for that reason.
        //
        // Dropped here rather than by going round the loop, and that is the
        // whole of why this is a loop of its own: nothing changed, so drawing
        // the frame again would only lay the message out afresh for a notch that
        // did nothing — and a wheel flicked hard leaves hundreds of them waiting,
        // which at a frame apiece takes seconds to get through. Those seconds
        // are what a tail has to outlive to reach the screen, so draining them
        // at once is what keeps `wheel_settle_ms` measuring the flick rather
        // than the redrawing. Anything that is not a notch — a keystroke, a
        // resize — ends the drain and is answered below as it always is.
        while (state.wheelLeftOver(event)) event = terminal.poll();

        // A resize is not a keystroke: the frame above has already been drawn to
        // the new size, and there is nothing else for it to mean.
        if (event == Event::Resize) continue;

        // Quitting is answered from any screen and ahead of everything modal,
        // the attributes dialog included: no box is worth being the one thing
        // standing between the user and the way out.
        if (state.keys.is(event, Command::AppQuit)) {
            terminal.exit();
            continue;
        }

        // The attributes dialog is modal and takes every key the layout has not
        // made `app.quit`: its chords are its own and no layout can move them —
        // Ctrl-C is Crash while it is up, which is what the list beside the
        // checkboxes says — so it is the one screen anywhere that can claim a
        // key back. It closes on Esc like every other box.
        if (state.attributePicker) {
            attributes_dialog::handleEvent(state, event);
            continue;
        }

        // The error box is modal in the same way, and stands where a screen
        // that would not open should have been: until it is acknowledged there
        // is nothing underneath for a key to mean.
        if (!state.errorMessage.empty()) {
            error_dialog::handleEvent(state, event);
            continue;
        }

        // The quit dialog is modal: while it is up it takes every key, so
        // nothing underneath can act on a keystroke aimed at the confirmation.
        if (state.confirm != AppState::Confirm::None) {
            // Taken before the answer, which is what puts the question away.
            const AppState::Confirm asked = state.confirm;
            const confirm_dialog::Outcome answer =
                confirm_dialog::handleEvent(state, event);
            if (answer == confirm_dialog::Outcome::Ignored) continue;
            state.confirm = AppState::Confirm::None;
            if (answer == confirm_dialog::Outcome::Dismissed) {
                // Only one question has anything to do with the second answer:
                // the commands found in a message are ignored and the message
                // is stored all the same. Everywhere else "no" means the thing
                // asked about does not happen, which is nothing to act on.
                if (asked == AppState::Confirm::ProcessCopies) {
                    screens::compose::ignoreCopies(state);
                }
                continue;
            }
            // Answered yes. What that means belongs to whoever asked, so the
            // question is put away first and then acted on.
            switch (asked) {
                case AppState::Confirm::Quit: terminal.exit(); break;
                case AppState::Confirm::SaveMessage:
                    screens::compose::saveMessage(state);
                    break;
                case AppState::Confirm::DropMessage:
                    screens::compose::dropMessage(state);
                    break;
                case AppState::Confirm::DeleteMessage:
                    screens::message_read::deleteMessage(state);
                    break;
                // Which of the two was asked is also what the message gets:
                // the template's notice at the head of it where it is somebody
                // else's, and nothing at all where it is the user's own.
                case AppState::Confirm::ChangeForeignMessage:
                    screens::compose::startChange(state, /*notice=*/true);
                    break;
                case AppState::Confirm::ChangeSentMessage:
                    screens::compose::startChange(state, /*notice=*/false);
                    break;
                case AppState::Confirm::ProcessCopies:
                    screens::compose::processCopies(state);
                    break;
                case AppState::Confirm::None: break;
            }
            continue;
        }

        // The context menu is modal in the same way, and stands over whichever
        // screen opened it until a command is picked or it is dismissed. What
        // the command means belongs to that screen, so the box is put away
        // first and the screen underneath asked afterwards — every one of those
        // commands can put a box of its own up, and two modals at once is not
        // something this loop can mean.
        if (state.menuView) {
            if (menu_dialog::handleEvent(state, event) == menu_dialog::Outcome::Picked) {
                const Command command = menu_dialog::current(state);
                state.menuView.reset();
                switch (state.navigator.current()) {
                    case app::ScreenId::MessageRead:
                        screens::message_read::runMenuCommand(state, command);
                        break;
                    case app::ScreenId::Compose:
                        screens::compose::runMenuCommand(state, command);
                        break;
                    case app::ScreenId::AreaList:
                        screens::area_list::runMenuCommand(state, command);
                        break;
                    // The message list carries no menu button, so it cannot be
                    // the screen a menu was opened from: marking the message
                    // under the cursor is its one command, and one button is no
                    // menu.
                    case app::ScreenId::MessageList: break;
                }
            }
            continue;
        }

        // The forward dialog is modal in the same way, and stands over the
        // reader until it is answered — with what is to become of the message,
        // which the area picker below then asks the where of. The two boxes are
        // one question in two halves, so the answer to the first is carried into
        // the second rather than acted on here.
        if (state.forwardPicker) {
            if (forward_dialog::handleEvent(state, event) ==
                forward_dialog::Outcome::Picked) {
                const auto mode = state.forwardPicker->mode;
                const bool marked = state.forwardPicker->marked;
                state.forwardPicker.reset();
                screens::message_read::askArea(state, purposeOf(mode), marked);
            }
            continue;
        }

        // The area picker is modal in the same way: it stands over the reader
        // until an area is picked for the message to go into. A reply or a
        // forward is then begun the way `q` and `e` begin a message here; a move
        // or a copy is done there and then, there being nothing to write.
        if (state.areaPicker) {
            if (area_dialog::handleEvent(state, event) == area_dialog::Outcome::Picked) {
                // Both copied out before the dialog is put away: what follows
                // opens and closes bases, and the manager's list is the only
                // thing holding the area.
                const domain::AreaConfig target =
                    manager.areas()[static_cast<size_t>(state.areaPicker->cursor)].config;
                const auto purpose = state.areaPicker->purpose;
                // Whether the scope box said the marked messages rather than the
                // one on screen. Only Move and Copy can be told that: a reply and
                // a forward are messages of the user's own, and the box does not
                // offer the set for either.
                const bool marked = state.areaPicker->marked;
                state.areaPicker.reset();
                switch (purpose) {
                    case AppState::AreaPicker::For::Reply:
                        screens::compose::startReplyElsewhere(state, target);
                        break;
                    case AppState::AreaPicker::For::Forward:
                        screens::compose::startForwardTo(state, target);
                        break;
                    case AppState::AreaPicker::For::Move:
                        if (marked) {
                            screens::message_read::moveMarked(state, target);
                        } else {
                            screens::message_read::moveMessage(state, target);
                        }
                        break;
                    case AppState::AreaPicker::For::Copy:
                        if (marked) {
                            screens::message_read::copyMarked(state, target);
                        } else {
                            screens::message_read::copyMessage(state, target);
                        }
                        break;
                }
            }
            continue;
        }

        // The import dialog is modal in the same way, and stands over the
        // editor until a file has been read or the question dropped. It does
        // the reading itself, being what is still on the screen when a file
        // will not open; what comes back here is the lines it came to, and
        // where they go in the message is the editor's business.
        if (state.importPicker) {
            if (import_dialog::handleEvent(state, event) ==
                import_dialog::Outcome::Imported) {
                // Taken out before the dialog is put away, which is what owns
                // them.
                const std::vector<std::string> lines =
                    std::move(state.importPicker->lines);
                state.importPicker.reset();
                screens::compose::insertImported(state, lines);
            }
            continue;
        }

        // The review box is modal in the same way, and stands over the editor
        // until it is answered. Its four answers are the whole of what can be
        // done with a message written elsewhere, so the box is put away before
        // any of them is acted on: three of the four can put a box of their own
        // up, and two modals at once is not something this loop can mean.
        if (state.externalReview) {
            if (external_dialog::handleEvent(state, event) ==
                external_dialog::Outcome::Picked) {
                const auto answer = state.externalReview->answer;
                state.externalReview.reset();
                switch (answer) {
                    // Straight to storing it, without the confirmation Ctrl-S
                    // raises: this box *is* that question, asked of a message
                    // the user has just been shown.
                    case AppState::ExternalReview::Answer::Save:
                        screens::compose::saveMessage(state);
                        break;
                    case AppState::ExternalReview::Answer::Drop:
                        screens::compose::dropMessage(state);
                        break;
                    case AppState::ExternalReview::Answer::Again:
                        screens::compose::requestExternalEditor(state);
                        break;
                    case AppState::ExternalReview::Answer::Header:
                        screens::compose::editHeader(state);
                        break;
                }
            }
            continue;
        }

        // The export asks what before it asks where, wherever the message
        // carries uuencoded files: the files taken out of it, or the message
        // written as the text it also is. What follows either answer stands in
        // this box's place, which is why the files are carried into it rather
        // than acted on here.
        //
        // A text export with something marked has one more question in front of
        // the file picker — which messages — and the files never do: they were
        // decoded out of the message on screen, and there is no set of them to
        // write.
        if (state.exportModePicker) {
            if (export_mode_dialog::handleEvent(state, event) ==
                export_mode_dialog::Outcome::Picked) {
                // Both taken off the box before it is put away, which is what
                // holds them.
                const bool decode =
                    state.exportModePicker->mode == AppState::ExportPicker::Mode::Uue;
                std::vector<app::UueFile> files =
                    std::move(state.exportModePicker->files);
                state.exportModePicker.reset();
                if (!decode && !state.marks.empty()) {
                    scope_dialog::open(state, AppState::ScopePicker::For::Export);
                } else {
                    export_dialog::open(
                        state, decode ? std::move(files) : std::vector<app::UueFile>{});
                }
            }
            continue;
        }

        // The export dialog is modal in the same way, and stands over the
        // reader until the message has been written or the question dropped. It
        // writes the file itself, being what is still on the screen when a file
        // will not open; there is nothing left here to do but put it away.
        if (state.exportPicker) {
            if (export_dialog::handleEvent(state, event) ==
                export_dialog::Outcome::Written) {
                state.exportPicker.reset();
            }
            continue;
        }

        // The find box is modal in the same way, and stands over the reader
        // until it has found something or been put away. It asks and does not
        // search: what it was answered with goes to the reader, which is what
        // walks the base and moves to the message. A search that came to
        // nothing leaves the box standing with the words still in it, saying so
        // in its bottom rule — the export box's habit, and for the same reason.
        if (state.findPicker) {
            if (find_dialog::handleEvent(state, event) == find_dialog::Outcome::Search) {
                // Copied out first: finding a message loads it, and nothing is
                // to be read back off the box across that.
                const std::string query = state.findPicker->query;
                const app::SearchScope scope = state.findPicker->scope;
                if (screens::message_read::findMessage(state, query, scope)) {
                    state.findPicker.reset();
                } else if (state.findPicker) {
                    state.findPicker->error = _("Not found");
                }
            }
            continue;
        }

        // The nodelist is modal in the same way. Opened by Ctrl-N it shows rather
        // than asks — every key either looks something up in it or puts it
        // away, and it does both itself. Opened from the compose screen it is
        // answering half of a To row, and the node picked is carried back to
        // the header the same way the area picker's answer is.
        if (state.nodelistView) {
            // Taken before the key is answered: the box may put itself away,
            // and what it was opened for is what says whether anything is
            // waiting on the answer.
            const auto purpose = state.nodelistView->purpose;
            const bool picked = nodelist_dialog::handleEvent(state, event) ==
                                nodelist_dialog::Outcome::Picked;
            const bool carbon =
                purpose == AppState::NodelistView::Purpose::PickCarbonCopy;
            if (picked) {
                // The node is taken off the box before it is put away, which is
                // what holds it.
                const auto node = nodelist_dialog::currentNode(state);
                state.nodelistView.reset();
                if (carbon) {
                    screens::compose::useCarbonCopy(state, node ? &*node : nullptr);
                } else if (node) {
                    screens::compose::useNode(state, purpose, *node);
                }
            } else if (carbon && !state.nodelistView) {
                // Closed without picking anybody. The message is waiting to be
                // stored and cannot wait on a box that is gone: that copy is
                // not made, and the editor is told so.
                screens::compose::useCarbonCopy(state, nullptr);
            }
            continue;
        }

        // The mark box is modal in the same way, and stands over the reader
        // until one of its five answers is picked or it is put away. The box is
        // gone before the marking is done, exactly as the menu is: what an
        // answer means belongs to the area underneath and not to the frame it
        // was picked in.
        if (state.markPicker) {
            if (mark_dialog::handleEvent(state, event) == mark_dialog::Outcome::Picked) {
                // Taken off the box before it is put away, which is what holds
                // it.
                const auto action = state.markPicker->action;
                state.markPicker.reset();
                mark_dialog::apply(state, action);
            }
            continue;
        }

        // The scope box is modal in the same way, and stands over the reader
        // until it is answered: which messages the key that opened it meant.
        // For `d` it is the confirmation as well, so an answer deletes there and
        // then; for `m` it is the first of three boxes and the answer decides
        // what the second one offers. Either way the box is put away first, as
        // every other modal's answer is acted on.
        if (state.scopePicker) {
            if (scope_dialog::handleEvent(state, event) ==
                scope_dialog::Outcome::Picked) {
                // Both taken off the box before it is put away, which is what
                // holds them.
                const auto purpose = state.scopePicker->purpose;
                const auto mode = state.scopePicker->mode;
                const bool marked = mode == AppState::ScopePicker::Mode::Marked;
                state.scopePicker.reset();
                // The way out is the one answer with nothing behind it, whichever
                // key asked.
                if (mode != AppState::ScopePicker::Mode::Cancel) {
                    switch (purpose) {
                        case AppState::ScopePicker::For::Delete:
                            if (marked) {
                                screens::message_read::deleteMarked(state);
                            } else {
                                screens::message_read::deleteMessage(state);
                            }
                            break;
                        case AppState::ScopePicker::For::Forward:
                            screens::message_read::askForward(state, marked);
                            break;
                        // The file picker in this box's place, told what it is
                        // writing. The files a message carries never reach here
                        // — see the export mode box above.
                        case AppState::ScopePicker::For::Export:
                            export_dialog::open(state, {}, marked);
                            break;
                    }
                }
            }
            continue;
        }

        // The info box is modal in the same way, and shows rather than asks:
        // every key either moves about inside it or puts it away, and it does
        // both itself.
        if (state.infoView) {
            info_dialog::handleEvent(state, event);
            continue;
        }

        // The list of replies is modal in the same way, and stands between the
        // reader and the key it was opened with.
        if (!state.replyChoices.empty()) {
            if (reply_dialog::handleEvent(state, event) ==
                reply_dialog::Outcome::Picked) {
                const uint32_t number =
                    state.replyChoices[static_cast<size_t>(state.replyChoice)].number;
                state.replyChoices.clear();
                screens::message_read::goToMessage(state, number);
            }
            continue;
        }

        // The hints along the bottom row, which belongs to no screen: a click on
        // one is answered with the key that hint is written under, and that key
        // is what the screen is then given. A click anywhere else comes through
        // as itself.
        const Event forScreen = hint_bar::clicked(state, event).value_or(event);

        // A keystroke that throws is swallowed rather than reported: a broken
        // area must not take the application down while the user is looking at
        // it, and there is nowhere on the screen left to say so — there is no
        // status line. The next frame is drawn from the state the failed
        // keystroke left, which is the state it found.
        //
        // The log is where it is said instead, naming the screen and the key, so
        // that a message which breaks the base it is in can be looked into
        // afterwards. Where the config named no `error_log` that is nowhere, and
        // this is the silence it has always been.
        try {
            switch (state.navigator.current()) {
                case app::ScreenId::AreaList:
                    screens::area_list::handleEvent(state, forScreen);
                    break;
                case app::ScreenId::MessageList:
                    screens::message_list::handleEvent(state, forScreen);
                    break;
                case app::ScreenId::MessageRead:
                    screens::message_read::handleEvent(state, forScreen);
                    break;
                case app::ScreenId::Compose:
                    screens::compose::handleEvent(state, forScreen);
                    break;
            }
        } catch (const std::exception& e) {
            error_log::write(screenName(state.navigator.current()),
                             eventName(forScreen) + ": " + std::string(e.what()));
        }

        // A key that put a different screen in front of the user takes the rest
        // of what was typed with it, the terminal being where those keys are
        // waiting. Leaving an area at its end is the one that asks for this: the
        // key doing it is held down, and the screen it lands on binds it to
        // something else.
        if (state.discardTypeahead) {
            state.discardTypeahead = false;
            terminal.flushInput();
        }
    }

    manager.closeCurrentArea();
    return 0;
}

}  // namespace amberedit::ui
