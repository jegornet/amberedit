#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "config/app_config.hpp"
#include "config/config_writer.hpp"
#include "config/embedded_resources.hpp"
#include "config/text_util.hpp"
#include "temp_dir.hpp"
#include "test_paths.hpp"
#include "test_strings.hpp"

using amberedit::config::AppConfig;
using amberedit::config::ConfigAnswers;
using amberedit::config::renderConfig;
using amberedit::config::renderConfigFrom;
using amberedit::config::TosserConfigFormat;
using amberedit::config::writeConfig;
using amberedit::test::contains;
using amberedit::test::errorOf;
using amberedit::test::valueOf;

namespace {

/// A full set of answers, as the wizard would hand them over.
ConfigAnswers answers() {
    ConfigAnswers a;
    a.userName = "Vasya Pupkin";
    a.address = "2:5020/9999.1";
    a.tosserConfigPath = "~/ftn/etc/areas";
    a.tosserFormat = TosserConfigFormat::Fidoconfig;
    a.defaultCharset = "CP866";
    a.composeCharset = "UTF-8";
    a.templatePath = amberedit::test::projectPath("default.tpl");
    return a;
}

/// How many lines of the config state the key — a commented sample is not one.
size_t stated(const std::string& text, const std::string& key) {
    size_t count = 0;
    for (const auto& line : amberedit::config::text::splitLines(text)) {
        if (amberedit::config::text::startsWith(line, key + " ")) ++count;
    }
    return count;
}

}  // namespace

TEST_CASE("the sample config in the binary is a config that loads [config_writer]") {
    // The whole of the wizard rests on this: it writes a config by filling the
    // sample in, so a sample that does not load is a wizard that cannot work.
    const auto parsed = AppConfig::loadFromString(
        std::string(amberedit::config::resources::exampleConfig()),
        "amberedit.cfg.example");
    CHECK_MESSAGE(parsed.has_value(), errorOf(parsed));
}

TEST_CASE("the sample in the binary is the file in the tree [config_writer]") {
    const auto onDisk = valueOf(amberedit::config::text::readFile(
        amberedit::test::projectPath("amberedit.cfg.example")));
    CHECK(std::string(amberedit::config::resources::exampleConfig()) == onDisk);
}

TEST_CASE("a rendered config carries every answer [config_writer]") {
    const std::string text = valueOf(renderConfig(answers()));
    const AppConfig config = valueOf(AppConfig::loadFromString(text, "written.cfg"));

    CHECK(config.userName == "Vasya Pupkin");
    REQUIRE(config.userAddress.has_value());
    CHECK(config.userAddress->toString() == "2:5020/9999.1");
    CHECK(config.tosserConfigFormat == TosserConfigFormat::Fidoconfig);
    CHECK(contains(config.tosserConfigPath, "ftn/etc/areas"));
    CHECK(config.defaultCharset == "CP866");
    CHECK(config.composeCharset == "UTF-8");
    CHECK(contains(config.templatePath, "default.tpl"));

    // Each of them once: the sample states them all, and a second line would be
    // refused by the parser as a setting written twice.
    for (const char* key : {"name", "address", "tosser_config", "tosser_config_format",
                            "default_charset", "compose_charset", "template"}) {
        CHECK_MESSAGE(stated(text, key) == 1, key);
    }
}

TEST_CASE("a rendered config is still the commented sample [config_writer]") {
    const std::string text = valueOf(renderConfig(answers()));

    // The point of writing the whole sample out is the documentation in it.
    CHECK(contains(text, "# --- who you are"));
    CHECK(contains(text, "# Each setting below says what it does"));
    CHECK(text.size() > amberedit::config::resources::exampleConfig().size() - 500);
}

TEST_CASE("the sample origin does not become the user's [config_writer]") {
    const std::string text = valueOf(renderConfig(answers()));
    const AppConfig config = valueOf(AppConfig::loadFromString(text, "written.cfg"));

    // It would otherwise stand at the foot of every echomail message written.
    CHECK(config.origin.empty());
    CHECK(contains(text, "#origin \"Somewhere in the world\""));
}

TEST_CASE("the tosser format is written as the config spells it [config_writer]") {
    ConfigAnswers a = answers();

    a.tosserFormat = TosserConfigFormat::AreasBbs;
    a.tosserConfigPath = "/ftn/etc/areas.bbs";
    CHECK(valueOf(AppConfig::loadFromString(valueOf(renderConfig(a)), "c"))
              .tosserConfigFormat == TosserConfigFormat::AreasBbs);

    a.tosserFormat = TosserConfigFormat::SquishCfg;
    a.tosserConfigPath = "/ftn/etc/squish.cfg";
    CHECK(valueOf(AppConfig::loadFromString(valueOf(renderConfig(a)), "c"))
              .tosserConfigFormat == TosserConfigFormat::SquishCfg);
}

TEST_CASE("a nodelist is written with the compiled file beside it [config_writer]") {
    ConfigAnswers a = answers();
    a.nodelistPath = "/home/ftn/nodelist/z2daily.999";
    a.nodelistDbPath = "/home/ftn/nodelist/amberndl.db";

    const std::string text = valueOf(renderConfig(a));
    const AppConfig config = valueOf(AppConfig::loadFromString(text, "written.cfg"));

    REQUIRE(config.nodelistSources.size() == 1);
    CHECK(config.nodelistSources[0] == "/home/ftn/nodelist/z2daily.999");
    CHECK(config.nodelistDbPath == "/home/ftn/nodelist/amberndl.db");
    // The other two samples are still samples.
    CHECK(stated(text, "nodelist") == 1);
    CHECK(contains(text, "#nodelist ~/ftn/nodelist/Z2PNT.Z99"));
}

TEST_CASE(
    "a skipped nodelist leaves the config saying nothing about one [config_writer]") {
    const std::string text = valueOf(renderConfig(answers()));
    const AppConfig config = valueOf(AppConfig::loadFromString(text, "written.cfg"));

    // A nodelist line with no nodelist_db beside it is refused by the config, so
    // the two go in together or not at all.
    CHECK(config.nodelistSources.empty());
    CHECK(config.nodelistDbPath.empty());
    CHECK(stated(text, "nodelist") == 0);
    CHECK(stated(text, "nodelist_db") == 0);
}

TEST_CASE("values that need quoting get it [config_writer]") {
    ConfigAnswers a = answers();
    a.userName = "Vasya  Pupkin";  // two spaces, which joining would eat
    a.tosserConfigPath = "/ftn/my configs/areas";

    const AppConfig config =
        valueOf(AppConfig::loadFromString(valueOf(renderConfig(a)), "written.cfg"));
    CHECK(config.userName == "Vasya  Pupkin");
    CHECK(config.tosserConfigPath == "/ftn/my configs/areas");
}

TEST_CASE("a hash in a value does not start a comment [config_writer]") {
    ConfigAnswers a = answers();
    a.userName = "Vasya #1 Pupkin";
    const AppConfig config =
        valueOf(AppConfig::loadFromString(valueOf(renderConfig(a)), "written.cfg"));
    CHECK(config.userName == "Vasya #1 Pupkin");
}

TEST_CASE("a double quote in a value is refused [config_writer]") {
    // The config format has no escape for one, so the choice is to refuse it or
    // to write a file that will not parse.
    ConfigAnswers a = answers();
    a.userName = "Vasya \"Pupkin\"";
    const auto rendered = renderConfig(a);
    REQUIRE_FALSE(rendered.has_value());
    CHECK(contains(rendered.error()->message(), "double quote"));
}

TEST_CASE("a sample without the line to fill in is a failure [config_writer]") {
    const auto missing = renderConfigFrom("name Somebody\n", answers());
    REQUIRE_FALSE(missing.has_value());
    CHECK_MESSAGE(contains(missing.error()->message(), "address"),
                  missing.error()->message());

    const std::string doubled =
        std::string(amberedit::config::resources::exampleConfig()) + "\nname Twice\n";
    const auto twice = renderConfigFrom(doubled, answers());
    REQUIRE_FALSE(twice.has_value());
    CHECK_MESSAGE(contains(twice.error()->message(), "name"), twice.error()->message());
}

TEST_CASE("writeConfig leaves a config a start can read [config_writer]") {
    const amberedit::test::TempDir dir;
    const std::string path = dir.path("amberedit.cfg");

    const auto written = writeConfig(path, answers());
    REQUIRE_MESSAGE(written.has_value(), errorOf(written));

    CHECK(std::filesystem::exists(path));
    CHECK_FALSE(std::filesystem::exists(path + ".new"));
    const auto loaded = AppConfig::loadFromFile(path);
    CHECK_MESSAGE(loaded.has_value(), errorOf(loaded));
}

TEST_CASE("writeConfig does not write over a config already there [config_writer]") {
    const amberedit::test::TempDir dir;
    const std::string path = dir.path("amberedit.cfg");
    REQUIRE(writeConfig(path, answers()).has_value());

    const auto again = writeConfig(path, answers());
    REQUIRE_FALSE(again.has_value());
    CHECK(contains(again.error()->message(), "already"));
}

TEST_CASE("writeConfig says so when the template is not there [config_writer]") {
    const amberedit::test::TempDir dir;
    const std::string path = dir.path("amberedit.cfg");
    ConfigAnswers a = answers();
    a.templatePath = dir.path("nothing.tpl");

    const auto written = writeConfig(path, a);
    REQUIRE_FALSE(written.has_value());
    CHECK_MESSAGE(contains(written.error()->message(), "template"),
                  written.error()->message());
    // And nothing is left behind, or the next --setup would refuse to run.
    CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("writeConfig refuses a directory that is not there [config_writer]") {
    const amberedit::test::TempDir dir;
    const auto written = writeConfig(dir.path("nowhere/amberedit.cfg"), answers());
    CHECK_FALSE(written.has_value());
}
