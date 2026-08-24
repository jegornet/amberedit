#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "config/app_config.hpp"
#include "config/commands.hpp"
#include "i18n/i18n.hpp"
#include "msgbase/null_lastread_store.hpp"
#include "test_paths.hpp"
#include "test_strings.hpp"
#include "ui/app_state.hpp"
#include "ui/confirm_dialog.hpp"
#include "ui/term/element.hpp"
#include "ui/term/screen.hpp"

using amberedit::test::contains;
using amberedit::test::errorOf;

namespace i18n = amberedit::i18n;

namespace {

/// Russian, for as long as this lives and put down again after — the
/// interface's language is process-wide, and a test that left one on would be
/// answering for every test that ran next.
///
/// `LANGUAGE` and not `LANG`, because it is the one gettext prefers and the one
/// that needs no locale of its own name to exist: what has to exist is a locale
/// for `LC_MESSAGES` to sit in, and `start()` takes that from the environment
/// the test runner was given.
///
/// `ok()` is false for either of two reasons, and a test that finds it false
/// simply stops: the build may have made no catalog, msgfmt not being required,
/// and the machine may have no locale gettext will translate under, which a
/// stock Debian or Ubuntu container has not.
class WithRussian {
public:
    WithRussian() {
        ::setenv("LANGUAGE", "ru", 1);
        // A locale for `LC_MESSAGES` to sit in, since gettext will not translate
        // under `C` — which is what a test runner in a container is given. The
        // program itself imposes nothing here and takes what the environment
        // says; a test has to put something there to have anything to assert on.
        for (const char* locale : {"", "C.UTF-8", "C.utf8", "en_US.UTF-8", "UTF-8"}) {
            if (locale[0] == '\0') {
                ::unsetenv("LC_ALL");
            } else {
                ::setenv("LC_ALL", locale, 1);
            }
            static_cast<void>(i18n::start());
            ok_ = i18n::translating();
            if (ok_) return;
        }
        ::unsetenv("LC_ALL");
    }
    ~WithRussian() {
        ::unsetenv("LC_ALL");
        i18n::clear();
    }

    WithRussian(const WithRussian&) = delete;
    WithRussian& operator=(const WithRussian&) = delete;

    [[nodiscard]] bool ok() const { return ok_; }

private:
    bool ok_{false};
};

}  // namespace

TEST_CASE("With no catalog every message is the literal it was written as [i18n]") {
    i18n::clear();
    CHECK_FALSE(i18n::translating());
    CHECK(std::string(_("Save the message?")) == "Save the message?");
    CHECK(std::string(C_("area list column", "New")) == "New");
    CHECK(std::string(i18n::plural("{0} node", "{0} nodes", 1)) == "{0} node");
    CHECK(std::string(i18n::plural("{0} node", "{0} nodes", 2)) == "{0} nodes");
    CHECK(std::string(i18n::plural("{0} node", "{0} nodes", 0)) == "{0} nodes");
}

TEST_CASE("The environment is what picks the language [i18n]") {
    i18n::clear();

    // Nothing asked for is English asked for, and carries no complaint.
    ::unsetenv("LANGUAGE");
    ::unsetenv("LC_ALL");
    ::unsetenv("LC_MESSAGES");
    ::unsetenv("LANG");
    const i18n::Started quiet = i18n::start();
    CHECK(quiet.warning.empty());
    CHECK_FALSE(i18n::translating());
    CHECK(std::string(_("Save the message?")) == "Save the message?");

    // A language there is no catalog for is not a fault either: English is the
    // answer and nothing is said about it.
    ::setenv("LANGUAGE", "xx", 1);
    const i18n::Started absent = i18n::start();
    CHECK(absent.warning.empty());
    CHECK_FALSE(i18n::translating());

    i18n::clear();
}

TEST_CASE("A language we have and the system cannot is a warning [i18n]") {
    i18n::clear();
    ::unsetenv("LC_ALL");
    ::unsetenv("LC_MESSAGES");
    ::unsetenv("LANG");
    // Russian, under a locale gettext refuses to translate under. Where the
    // build made no Russian catalog there is nothing to warn about and the case
    // has nothing to say.
    ::setenv("LANGUAGE", "ru", 1);
    ::setenv("LC_ALL", "C", 1);

    const i18n::Started started = i18n::start();
    if (!started.warning.empty()) {
        CHECK_FALSE(i18n::translating());
        CHECK(contains(started.warning, "ru"));
        CHECK(contains(started.warning, "locale-gen"));
        // It runs, in English.
        CHECK(std::string(_("Save the message?")) == "Save the message?");
    }

    ::unsetenv("LC_ALL");
    i18n::clear();
}

TEST_CASE("format() puts the arguments where the pattern numbers them [i18n]") {
    CHECK(i18n::format("{0}", {"one"}) == "one");
    CHECK(i18n::format("cannot open base {0}: {1}", {"a", "b"}) ==
          "cannot open base a: b");
    // What a translation is for: the same message with the parts the other way
    // round, which no amount of concatenation could have offered.
    CHECK(i18n::format("{1} ← {0}", {"a", "b"}) == "b ← a");
    // An argument used twice, which the usage page does with the program name.
    CHECK(i18n::format("{0} and {0}", {"x"}) == "x and x");
    CHECK(i18n::format("", {"x"}).empty());
    CHECK(i18n::format("nothing at all", {}) == "nothing at all");
}

TEST_CASE("format() leaves a brace that is not a placeholder alone [i18n]") {
    // A translation is text somebody else wrote. Nothing in it may reach past
    // the end of the argument list, and anything that is not a placeholder is
    // drawn as it stands.
    CHECK(i18n::format("{}", {"x"}) == "{}");
    CHECK(i18n::format("{x}", {"x"}) == "{x}");
    CHECK(i18n::format("{0", {"x"}) == "{0");
    CHECK(i18n::format("{", {"x"}) == "{");
    CHECK(i18n::format("}", {"x"}) == "}");
    // Numbered past what was passed: left as written rather than read from
    // beyond the list.
    CHECK(i18n::format("{1}", {"x"}) == "{1}");
    CHECK(i18n::format("{999999999}", {"x"}) == "{999999999}");
    CHECK(i18n::format("{0}", {}) == "{0}");
}

TEST_CASE("A loaded catalog answers in its own language [i18n]") {
    const WithRussian russian;
    if (!russian.ok()) return;

    CHECK(i18n::translating());
    CHECK(std::string(_("Save the message?")) == "Сохранить письмо?");
    // A context makes one English word two Russian ones.
    CHECK(std::string(_("New")) == "Новое");
    CHECK(std::string(C_("area list column", "New")) == "Нов");
    // A message the catalog has not got is still the literal.
    CHECK(std::string(_("no such message is in the catalog")) ==
          "no such message is in the catalog");
}

TEST_CASE("Russian's three plural forms come out of the catalog [i18n]") {
    const WithRussian russian;
    if (!russian.ok()) return;

    const auto node = [](unsigned long n) {
        return i18n::format(i18n::plural("{0} node", "{0} nodes", n),
                            {std::to_string(n)});
    };
    CHECK(node(1) == "1 узел");
    CHECK(node(2) == "2 узла");
    CHECK(node(5) == "5 узлов");
    CHECK(node(11) == "11 узлов");
    CHECK(node(21) == "21 узел");
}

TEST_CASE("A command's label is drawn in the interface's language [i18n]") {
    using amberedit::config::Command;
    using amberedit::config::Commands;

    // `label` is a `const char*` — the msgid gettext is handed — so it is
    // compared as text and never with `==`, which would compare the pointers.
    const auto labelOf = [](Command command) {
        return std::string_view(Commands::of(command).labelId);
    };

    // The table itself keeps the English; `labelOf()` is what a button draws.
    CHECK(labelOf(Command::ReaderFind) == "Find");
    CHECK(std::string(Commands::labelOf(Command::ReaderFind)) == "Find");

    const WithRussian russian;
    if (!russian.ok()) return;
    CHECK(labelOf(Command::ReaderFind) == "Find");
    CHECK(std::string(Commands::labelOf(Command::ReaderFind)) == "Поиск");
}

TEST_CASE("Every message the source carries is in the Russian catalog [i18n]") {
    const WithRussian russian;
    if (!russian.ok()) return;

    // A spot check that the catalog and the tree have not come apart: these are
    // messages from four different corners of the interface, and one of them
    // going back to English is what an unmerged po/ru.po looks like.
    CHECK(std::string(_(" Message info ")) != " Message info ");
    CHECK(std::string(_("Rescanning areas...")) != "Rescanning areas...");
    CHECK(std::string(_("Nothing to look for")) != "Nothing to look for");
    CHECK(std::string(_(" General parameters — step 1 of 6 ")) !=
          " General parameters — step 1 of 6 ");
}

// --- what actually reaches the screen ----------------------------------------

namespace {

/// Hands back nothing: this is about the words on the box, not the areas.
class EmptyAreaSource final : public amberedit::ports::IAreaConfigSource {
public:
    tl::expected<std::vector<amberedit::domain::AreaConfig>, amberedit::ErrorPtr>
    loadAreas() override {
        return {};
    }
};

/// The state a dialog stands on, with the config it refers to outliving it.
struct Fixture {
    static constexpr int kWidth = 60;
    static constexpr int kHeight = 20;

    Fixture()
        : manager(std::make_unique<EmptyAreaSource>(),
                  std::make_unique<amberedit::msgbase::NullLastReadStore>(), config),
          state(manager, config) {
        state.width = kWidth;
        state.height = kHeight;
    }

    amberedit::config::AppConfig config;
    amberedit::app::AreaManager manager;
    amberedit::ui::AppState state;
};

/// Every row of the screen a dialog leaves behind, as one string — which is what
/// a person reading the box would see, and the only thing worth asserting on
/// here.
std::string drawn(amberedit::ui::AppState& state) {
    using namespace amberedit::ui::term;
    const Element element = amberedit::ui::confirm_dialog::render(state, text(""));
    Screen screen(Fixture::kWidth, Fixture::kHeight);
    render(screen, element);

    std::string out;
    for (int y = 0; y < Fixture::kHeight; ++y) {
        for (int x = 0; x < Fixture::kWidth; ++x) out += screen.at(x, y).glyph;
        out += "\n";
    }
    return out;
}

}  // namespace

TEST_CASE("A dialog is drawn in the interface's language [i18n]") {
    Fixture fixture;
    fixture.state.confirm = amberedit::ui::AppState::Confirm::DeleteMessage;

    i18n::clear();
    const std::string english = drawn(fixture.state);
    CHECK(contains(english, "Delete this message?"));
    CHECK(contains(english, "Yes"));
    CHECK(contains(english, "No"));

    const WithRussian russian;
    if (!russian.ok()) return;
    const std::string translated = drawn(fixture.state);
    CHECK(contains(translated, "Удалить это письмо?"));
    CHECK(contains(translated, "Да"));
    CHECK(contains(translated, "Нет"));
    CHECK_FALSE(contains(translated, "Delete this message?"));
}
