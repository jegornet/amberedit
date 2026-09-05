#include <doctest/doctest.h>

#include "temp_squish_base.hpp"
#include "ui/area_fixture.hpp"
#include "ui/focus.hpp"
#include "ui/help_dialog.hpp"
#include "ui/info_dialog.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/term/event.hpp"

using amberedit::test::AreaFixture;
using amberedit::test::TempSquishBase;
using amberedit::ui::Addressee;
using amberedit::ui::Focus;
using amberedit::ui::focusOf;
using amberedit::ui::term::Event;

namespace help_dialog = amberedit::ui::help_dialog;
namespace info_dialog = amberedit::ui::info_dialog;
namespace message_list = amberedit::ui::screens::message_list;
namespace message_read = amberedit::ui::screens::message_read;
namespace term = amberedit::ui::term;

namespace {

/// A notch of the wheel, up or down.
Event wheel(bool down) {
    term::MouseEvent mouse;
    mouse.button =
        down ? term::MouseEvent::Button::WheelDown : term::MouseEvent::Button::WheelUp;
    mouse.motion = term::MouseEvent::Motion::Pressed;
    return Event::Mouse(mouse);
}

}  // namespace

TEST_CASE("Opening an area and a message each change the focus [focus][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());

    const Focus onList = focusOf(fixture.state);
    CHECK(onList.addressee == Addressee::Screen);

    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    CHECK(focusOf(fixture.state) != onList);
}

TEST_CASE("Walking to the next message changes the focus [focus][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    message_read::openMessage(fixture.state, 1);

    // What the tail of a flick would otherwise go on scrolling: neither the
    // screen nor the box over it changes when → walks to the next message, and
    // the message under the wheel is another one all the same.
    const Focus first = focusOf(fixture.state);
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));
    const Focus second = focusOf(fixture.state);
    CHECK(second.screen == first.screen);
    CHECK(second.addressee == first.addressee);
    CHECK(second != first);

    // And back the other way, which is the same walk and the same tail.
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowLeft));
    CHECK(focusOf(fixture.state) == first);
}

TEST_CASE("Scrolling the message leaves the focus alone [focus][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    message_read::openMessage(fixture.state, 1);

    const Focus before = focusOf(fixture.state);
    for (int i = 0; i < 5; ++i) {
        static_cast<void>(message_read::handleEvent(fixture.state, wheel(true)));
    }
    CHECK(focusOf(fixture.state) == before);
}

TEST_CASE("The wheel over the message list leaves the focus alone [focus][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.height = 24;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    // `l` is what the reader opens the list with, the same as the list's own
    // tests use it.
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('l')));
    REQUIRE(fixture.state.navigator.current() == amberedit::app::ScreenId::MessageList);

    // The one thing the focus must never say. The wheel moves the list's cursor
    // a row at a time, and a cursor read into the focus would make every notch
    // of a flick a change — which the guard would answer by swallowing the rest
    // of it, leaving the list stuck a row from where it started.
    const Focus before = focusOf(fixture.state);
    for (int i = 0; i < 5; ++i) {
        static_cast<void>(message_list::handleEvent(fixture.state, wheel(true)));
    }
    REQUIRE(fixture.state.messageCursor != 0);
    CHECK(focusOf(fixture.state) == before);
}

TEST_CASE("A box opening over the reader changes the focus [focus][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    message_read::openMessage(fixture.state, 1);

    const Focus behind = focusOf(fixture.state);
    message_read::openMenu(fixture.state);
    REQUIRE(fixture.state.menuView);
    const Focus underMenu = focusOf(fixture.state);
    CHECK(underMenu.screen == behind.screen);
    CHECK(underMenu.addressee == Addressee::Menu);

    // One box giving way to another is a change as much as one opening is: the
    // list inside the new box is not the one the flick was aimed at.
    fixture.state.menuView.reset();
    info_dialog::open(fixture.state);
    REQUIRE(fixture.state.infoView);
    CHECK(focusOf(fixture.state) != underMenu);
    CHECK(focusOf(fixture.state).addressee == Addressee::Info);

    // The help box is the last of the chain and the one any screen opens, so it
    // is the one most easily left out of this list.
    fixture.state.infoView.reset();
    help_dialog::open(fixture.state);
    REQUIRE(fixture.state.helpView);
    CHECK(focusOf(fixture.state).addressee == Addressee::Help);

    // And putting it away comes back to what was behind it.
    fixture.state.helpView.reset();
    CHECK(focusOf(fixture.state) == behind);
}
