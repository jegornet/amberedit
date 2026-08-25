#include "app/url_handler.hpp"

#include <doctest/doctest.h>

#include <sys/stat.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "temp_dir.hpp"
#include "test_strings.hpp"

using amberedit::app::runUrlHandler;
using amberedit::app::urlHandlerCommand;
using amberedit::test::contains;
using amberedit::test::errorOf;
using amberedit::test::TempDir;

namespace {

/// A program that writes its first argument to `output` and nothing else:
/// what a handler is asked to do is open the link, and what the test can see
/// of that is the link the program was handed.
std::string aProgramWriting(const TempDir& dir, const std::string& output) {
    const std::string path = dir.path("open");
    std::ofstream file(path);
    file << "#!/bin/sh\nprintf '%s' \"$1\" > \"" << output << "\"\n";
    file.close();
    REQUIRE(::chmod(path.c_str(), 0755) == 0);
    return path;
}

std::string contentsOf(const std::string& path) {
    std::ifstream file(path);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

}  // namespace

TEST_CASE("The link goes wherever $url stands [urlhandler]") {
    CHECK(urlHandlerCommand({"lynx", "$url"}, "http://ftn.example/x") ==
          std::vector<std::string>{"lynx", "http://ftn.example/x"});
    // Inside an argument as readily as alone in one, and every time it is
    // written: a script may want the link twice and say so.
    CHECK(urlHandlerCommand({"open", "--url=$url", "--also=$url"}, "gopher://x") ==
          std::vector<std::string>{"open", "--url=gopher://x", "--also=gopher://x"});
    // A link that spells the placeholder itself is left as it is: what is put
    // in its place is not looked at again.
    CHECK(urlHandlerCommand({"open", "$url"}, "http://x/$url") ==
          std::vector<std::string>{"open", "http://x/$url"});
}

TEST_CASE("A handler runs with the link it was given [urlhandler]") {
    TempDir dir;
    const std::string opened = dir.path("opened");
    const std::string program = aProgramWriting(dir, opened);

    CHECK(runUrlHandler({program, "$url"}, "http://ftn.example/x?a=1&b=2").has_value());
    CHECK(contentsOf(opened) == "http://ftn.example/x?a=1&b=2");
}

TEST_CASE("A handler is looked for on $PATH [urlhandler]") {
    // `true` takes whatever it is handed and says it went well, and every Unix
    // has one somewhere on the path — which is the whole of what is asserted
    // here: the name was not a path and was found all the same.
    CHECK(runUrlHandler({"true", "$url"}, "http://ftn.example/x").has_value());
}

TEST_CASE("A handler that cannot be run is said so, by name [urlhandler]") {
    const std::string error =
        errorOf(runUrlHandler({"amberedit-no-such-browser", "$url"}, "http://x"));
    CHECK_MESSAGE(contains(error, "amberedit-no-such-browser"), error);
}

TEST_CASE("A config naming no handler runs nothing [urlhandler]") {
    CHECK(runUrlHandler({}, "http://ftn.example/x").has_value());
}
