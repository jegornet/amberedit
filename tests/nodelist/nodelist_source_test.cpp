#include "nodelist/nodelist_source.hpp"

#include <doctest/doctest.h>

#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "config/text_util.hpp"
#include "temp_dir.hpp"
#include "test_paths.hpp"
#include "test_strings.hpp"

using namespace amberedit;
using amberedit::test::contains;
namespace fs = std::filesystem;

namespace {

/// A file with something in it, stamped at the stated moment — which is what
/// decides which of several nodelists is the newest.
void writeAt(const std::string& path, fs::file_time_type written) {
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "Zone,2,A,B,C,D,300\r\n";
    }
    std::error_code ec;
    fs::last_write_time(path, written, ec);
    REQUIRE_FALSE(ec);
}

fs::file_time_type minutesAgo(int minutes) {
    return fs::file_time_type::clock::now() - std::chrono::minutes(minutes);
}

}  // namespace

TEST_CASE("a nodelist line says which of the three kinds of name it is [nodelist]") {
    using nodelist::NodelistSpec;
    using nodelist::SpecKind;

    const auto exact = NodelistSpec::of("/ftn/nodelist/nodelist.ndl");
    CHECK(exact.kind == SpecKind::Exact);

    const auto daily = NodelistSpec::of("/ftn/nodelist/Z2DAILY.999");
    CHECK(daily.kind == SpecKind::DayNumber);
    CHECK(daily.stem == "Z2DAILY");
    CHECK(daily.directory == "/ftn/nodelist");

    const auto zipped = NodelistSpec::of("/ftn/nodelist/Z2PNT.Z99");
    CHECK(zipped.kind == SpecKind::ZipArchive);
    CHECK(zipped.stem == "Z2PNT");

    // Either case of the letter, as the archives come spelled both ways.
    CHECK(NodelistSpec::of("/ftn/nodelist/Z2PNT.z99").kind == SpecKind::ZipArchive);

    // Anything else is a filename, including a day number that is not the
    // pattern: `.225` is one file and `.999` is whichever file is newest.
    CHECK(NodelistSpec::of("/ftn/nodelist/Z2DAILY.225").kind == SpecKind::Exact);
    CHECK(NodelistSpec::of("nodelist").kind == SpecKind::Exact);
}

TEST_CASE("the two patterns may be written as wildcards instead [nodelist]") {
    using nodelist::NodelistSpec;
    using nodelist::SpecKind;

    // `.*` is `.999` and `.z*` is `.Z99`, for somebody who would rather say
    // "whatever is there" than remember which sentinel means which.
    const auto daily = NodelistSpec::of("/ftn/nodelist/z2daily.*");
    CHECK(daily.kind == SpecKind::DayNumber);
    CHECK(daily.stem == "z2daily");

    const auto zipped = NodelistSpec::of("/ftn/nodelist/z2daily.z*");
    CHECK(zipped.kind == SpecKind::ZipArchive);
    CHECK(zipped.stem == "z2daily");
    CHECK(NodelistSpec::of("/ftn/nodelist/Z2PNT.Z*").kind == SpecKind::ZipArchive);

    // The name in front of the extension is a glob in all three kinds.
    const auto globbed = NodelistSpec::of("/ftn/nodelist/z2*.999");
    CHECK(globbed.kind == SpecKind::DayNumber);
    CHECK(globbed.stem == "z2*");

    // A wildcard against an extension that names no kind is a glob over the
    // whole filename, and the kind stays what any other filename's is.
    const auto whole = NodelistSpec::of("/ftn/nodelist/nodelist.n*");
    CHECK(whole.kind == SpecKind::Exact);
    CHECK(whole.stem == "nodelist.n*");
}

TEST_CASE("a wildcard picks the newest file it covers [nodelist]") {
    test::TempDir dir;

    writeAt(dir.path("Z2DAILY.225"), minutesAgo(30));
    writeAt(dir.path("Z2DAILY.229"), minutesAgo(5));
    writeAt(dir.path("Z2DAILY.226"), minutesAgo(60));
    writeAt(dir.path("Z2DAILY.TXT"), minutesAgo(1));

    // `.*` covers exactly what `.999` covers, the `.TXT` beside them left out —
    // that is the whole of what "works the same as" means here.
    const auto wildcard =
        nodelist::newestMatch(nodelist::NodelistSpec::of(dir.path("z2daily.*")));
    REQUIRE(wildcard);
    CHECK(fs::path(*wildcard).filename() == "Z2DAILY.229");

    const auto sentinel =
        nodelist::newestMatch(nodelist::NodelistSpec::of(dir.path("Z2DAILY.999")));
    REQUIRE(sentinel);
    CHECK(*sentinel == *wildcard);

    // A glob over the name reaches every nodelist whose name it describes, and
    // the newest of the lot is the one taken.
    writeAt(dir.path("Z2PNT.230"), minutesAgo(1));
    const auto across =
        nodelist::newestMatch(nodelist::NodelistSpec::of(dir.path("z2*.*")));
    REQUIRE(across);
    CHECK(fs::path(*across).filename() == "Z2PNT.230");

    // A glob over a whole filename is a glob like any other, and the extension
    // is then no longer a pattern but text the glob has to match.
    const auto exact =
        nodelist::newestMatch(nodelist::NodelistSpec::of(dir.path("z2daily.2*")));
    REQUIRE(exact);
    CHECK(fs::path(*exact).filename() == "Z2DAILY.229");
}

TEST_CASE("a wildcard that covers nothing says what it was looking for [nodelist]") {
    test::TempDir dir;
    writeAt(dir.path("NODELIST.TXT"), minutesAgo(0));

    nodelist::NodelistSources sources(dir.path("tmp"));
    const std::string error =
        amberedit::test::errorOf(sources.read(dir.path("nodelist.*"), ""));
    CHECK_MESSAGE(contains(error, "001 to 366"), error);
    const std::string error2 =
        amberedit::test::errorOf(sources.read(dir.path("nodelist.z*"), ""));
    CHECK_MESSAGE(contains(error2, "Z and two digits"), error2);
    // A glob over a whole filename has no kind to describe, so it says only
    // that nothing answered to it — and never "nodelist not found", which
    // would name a file nobody wrote.
    const std::string error3 =
        amberedit::test::errorOf(sources.read(dir.path("nothing*.ndl"), ""));
    CHECK_MESSAGE(contains(error3, "no nodelist matching"), error3);
}

TEST_CASE("the newest of the files a pattern covers is the one taken [nodelist]") {
    test::TempDir dir;

    writeAt(dir.path("NODELIST.225"), minutesAgo(30));
    writeAt(dir.path("NODELIST.229"), minutesAgo(5));
    writeAt(dir.path("NODELIST.226"), minutesAgo(60));
    // Not a day number, and so not one of the files the pattern covers — which
    // is what lets `999` stand for the pattern and never for a file.
    writeAt(dir.path("NODELIST.999"), minutesAgo(1));
    writeAt(dir.path("NODELIST.TXT"), minutesAgo(1));
    // Another nodelist entirely, sharing the directory as they do.
    writeAt(dir.path("PNTLIST.230"), minutesAgo(1));

    const auto newest =
        nodelist::newestMatch(nodelist::NodelistSpec::of(dir.path("NODELIST.999")));
    REQUIRE(newest);
    CHECK(fs::path(*newest).filename() == "NODELIST.229");

    // The number breaks a tie, which is what unpacking a batch of them at once
    // leaves behind.
    const auto together = minutesAgo(2);
    writeAt(dir.path("NODELIST.225"), together);
    writeAt(dir.path("NODELIST.229"), together);
    writeAt(dir.path("NODELIST.226"), together);
    const auto tied =
        nodelist::newestMatch(nodelist::NodelistSpec::of(dir.path("NODELIST.999")));
    REQUIRE(tied);
    CHECK(fs::path(*tied).filename() == "NODELIST.229");
}

TEST_CASE("a pattern that covers nothing says what it was looking for [nodelist]") {
    test::TempDir dir;
    writeAt(dir.path("NODELIST.TXT"), minutesAgo(0));

    nodelist::NodelistSources sources(dir.path("tmp"));
    const std::string error =
        amberedit::test::errorOf(sources.read(dir.path("NODELIST.999"), ""));
    CHECK_MESSAGE(contains(error, "001 to 366"), error);
    const std::string error2 =
        amberedit::test::errorOf(sources.read(dir.path("NODELIST.Z99"), ""));
    CHECK_MESSAGE(contains(error2, "Z and two digits"), error2);
    const std::string error3 =
        amberedit::test::errorOf(sources.read(dir.path("nowhere.ndl"), ""));
    CHECK_MESSAGE(contains(error3, "nodelist not found"), error3);
}

TEST_CASE("the charset a nodelist line states is part of its state [nodelist]") {
    test::TempDir dir;
    const std::string path = dir.path("NODELIST.225");

    // A line naming a file that is not there has a state like any other: it is
    // what stops the next start from trying to compile it again.
    const auto missing = nodelist::stateOf(path, "CP866");
    CHECK(missing.spec == path);
    CHECK(missing.charset == "CP866");
    CHECK(missing.path.empty());
    CHECK(missing.size == 0);

    writeAt(path, minutesAgo(5));
    const auto present = nodelist::stateOf(path, "CP866");
    CHECK(present.path == path);
    CHECK(present.size > 0);
    CHECK(present != missing);

    // A line whose charset has been corrected is a line that has to be read
    // again, though the file has not moved.
    CHECK(nodelist::stateOf(path, "KOI8-R") != present);
}

TEST_CASE("a nodelist is read in the charset the line states [nodelist]") {
    test::TempDir dir;
    const std::string path = dir.path("NODELIST.225");
    {
        // "Москва" in CP866, which is what a Russian nodelist is written in and
        // what nothing but the config line can say.
        std::ofstream out(path, std::ios::binary);
        out << "Zone,2,Europe,Somewhere,Nobody,-Unpublished-,300\r\n"
            << "Host,6000,Some_Net,\x8C\xAE\xE1\xAA\xA2\xA0,Some_Sysop,"
               "-Unpublished-,300\r\n";
    }

    nodelist::NodelistSources sources(dir.path("tmp"));
    const auto loaded = amberedit::test::valueOf(sources.read(path, "CP866"));
    // Everything above the file is UTF-8, here as everywhere else.
    CHECK(contains(loaded.text, "Москва"));

    // And a line that states none is read in the locale's, which for a file of
    // ASCII is every charset there is.
    const auto bare = amberedit::test::valueOf(
        sources.read(test::projectPath("testdata/nodelist/Z2DAILY.225"), ""));
    CHECK(config::text::startsWith(bare.text, ";A FidoNet Nodelist"));
}

TEST_CASE("a nodelist named by its own name is read as it stands [nodelist]") {
    test::TempDir dir;
    nodelist::NodelistSources sources(dir.path("tmp"));

    const auto loaded = amberedit::test::valueOf(
        sources.read(test::projectPath("testdata/nodelist/Z2DAILY.225"), ""));
    CHECK(loaded.archive.empty());
    CHECK(config::text::startsWith(loaded.text, ";A FidoNet Nodelist"));
}

TEST_CASE("a day-number pattern finds the nodelist in testdata [nodelist]") {
    test::TempDir dir;
    nodelist::NodelistSources sources(dir.path("tmp"));

    const auto loaded = amberedit::test::valueOf(
        sources.read(test::projectPath("testdata/nodelist/Z2DAILY.999"), ""));
    CHECK(fs::path(loaded.readFrom).filename() == "Z2DAILY.225");
    CHECK(loaded.archive.empty());
    CHECK(config::text::startsWith(loaded.text, ";A FidoNet Nodelist"));
}

TEST_CASE("a zipped pointlist is unpacked without paths and taken away again "
          "[nodelist]") {
    test::TempDir dir;
    const std::string temporary = dir.path("tmp");
    const std::string archive = test::projectPath("testdata/nodelist/Z2PNT.Z99");

    {
        nodelist::NodelistSources sources(temporary);
        const auto loaded = amberedit::test::valueOf(sources.read(archive, ""));

        // The archive holds Z2PNT.219, and it is unpacked under that name and
        // in the temporary directory — not under whatever path the archive may
        // have stored in front of it.
        CHECK(fs::path(loaded.readFrom).filename() == "Z2PNT.219");
        CHECK(fs::path(loaded.readFrom).parent_path() == fs::path(temporary));
        CHECK(fs::path(loaded.archive).filename() == "Z2PNT.Z19");
        CHECK(fs::exists(loaded.readFrom));
        CHECK(config::text::startsWith(loaded.text, ";A Zone 2 Fidonet pointlist"));

        // Nothing else in the archive is written anywhere.
        size_t written = 0;
        for (const auto& item : fs::directory_iterator(temporary)) {
            static_cast<void>(item);
            ++written;
        }
        CHECK(written == 1);
    }

    // And what was unpacked is gone once the sources are.
    CHECK(fs::is_empty(temporary));
}

TEST_CASE("an archive named outright is unpacked all the same [nodelist]") {
    test::TempDir dir;
    const std::string temporary = dir.path("tmp");

    // A distribution that is replaced in place rather than day-numbered —
    // `MICRONET.ZIP` holding `MICRONET.240` — is named by its own name, and
    // there is no sentinel to write for it. What was found says it is an
    // archive; the line that found it does not have to.
    const std::string archive = dir.path("Z2PNT.ZIP");
    fs::copy_file(test::projectPath("testdata/nodelist/Z2PNT.Z19"), archive);

    nodelist::NodelistSources sources(temporary);
    const auto loaded = amberedit::test::valueOf(sources.read(archive, ""));

    CHECK(fs::path(loaded.archive).filename() == "Z2PNT.ZIP");
    CHECK(fs::path(loaded.readFrom).filename() == "Z2PNT.219");
    CHECK(fs::path(loaded.readFrom).parent_path() == fs::path(temporary));
    CHECK(config::text::startsWith(loaded.text, ";A Zone 2 Fidonet pointlist"));

    // The state is the archive itself, as it is for a `.Z99` line: that is the
    // file a start can stat without unpacking anything.
    CHECK(loaded.state.path == archive);
    CHECK(loaded.state.size == fs::file_size(archive));
}

TEST_CASE("a counter-numbered archive named outright is unpacked too [nodelist]") {
    test::TempDir dir;
    nodelist::NodelistSources sources(dir.path("tmp"));

    // `Z2PNT.Z19` and not `Z2PNT.Z99`: the line names one particular archive
    // and no pattern at all, and it is still an archive.
    const auto loaded = amberedit::test::valueOf(
        sources.read(test::projectPath("testdata/nodelist/Z2PNT.Z19"), ""));

    CHECK(fs::path(loaded.archive).filename() == "Z2PNT.Z19");
    CHECK(fs::path(loaded.readFrom).filename() == "Z2PNT.219");
    CHECK(config::text::startsWith(loaded.text, ";A Zone 2 Fidonet pointlist"));
}

TEST_CASE("the wildcard spellings find the same files in testdata [nodelist]") {
    test::TempDir dir;
    nodelist::NodelistSources sources(dir.path("tmp"));

    // `.*` where `.999` stood.
    const auto daily = amberedit::test::valueOf(
        sources.read(test::projectPath("testdata/nodelist/z2daily.*"), ""));
    CHECK(fs::path(daily.readFrom).filename() == "Z2DAILY.225");
    CHECK(daily.archive.empty());
    CHECK(config::text::startsWith(daily.text, ";A FidoNet Nodelist"));

    // `.z*` where `.Z99` stood — and the pointlist inside the archive is still
    // found, since it is the archive's own name that names it and not the line
    // that found the archive.
    const auto zipped = amberedit::test::valueOf(
        sources.read(test::projectPath("testdata/nodelist/z2pnt.z*"), ""));
    CHECK(fs::path(zipped.archive).filename() == "Z2PNT.Z19");
    CHECK(fs::path(zipped.readFrom).filename() == "Z2PNT.219");
    CHECK(config::text::startsWith(zipped.text, ";A Zone 2 Fidonet pointlist"));

    // A glob over the name reaches it too.
    const auto globbed = amberedit::test::valueOf(
        sources.read(test::projectPath("testdata/nodelist/z2*.z*"), ""));
    CHECK(fs::path(globbed.archive).filename() == "Z2PNT.Z19");
    CHECK(fs::path(globbed.readFrom).filename() == "Z2PNT.219");
}

TEST_CASE(
    "a config naming no tmpdir unpacks under the system's temporary directory "
    "[nodelist]") {
    // The system's own temporary directory is what `tmpdir` falls back on, and
    // $TMPDIR is what says where that is — so pointing it at a directory of the
    // test's own is the whole of standing in for a machine here.
    test::TempDir dir;
    fs::create_directories(dir.path("system"));
    test::WithTempDirEnv env(dir.path("system"));

    const std::string expected =
        dir.path("system") + "/amberedit-" + std::to_string(::getuid());

    {
        nodelist::NodelistSources sources("");
        const auto loaded = amberedit::test::valueOf(
            sources.read(test::projectPath("testdata/nodelist/Z2PNT.Z99"), ""));

        CHECK(fs::path(loaded.readFrom).filename() == "Z2PNT.219");
        CHECK(fs::path(loaded.readFrom).parent_path() == fs::path(expected));
        CHECK(config::text::startsWith(loaded.text, ";A Zone 2 Fidonet pointlist"));
    }

    // What was unpacked is gone; the directory stays, which is what keeps the
    // name ours between one run and the next.
    CHECK(fs::is_directory(expected));
    CHECK(fs::is_empty(expected));
}

TEST_CASE(
    "a directory that cannot be worked in says what it was wanted for "
    "[nodelist]") {
    // What is wrong with the directory is `makeTempDir`'s to say — the cases are
    // its own tests — and which nodelist wanted one is what the message would be
    // missing without this. A machine whose $TMPDIR is not a directory has
    // nothing to fall back on, which is the shortest way to a directory that
    // cannot be worked in.
    test::TempDir dir;
    test::WithTempDirEnv env(dir.path("not-a-directory"));
    const std::string archive = test::projectPath("testdata/nodelist/Z2PNT.Z99");

    nodelist::NodelistSources sources("");
    const std::string error = amberedit::test::errorOf(sources.read(archive, ""));
    CHECK_MESSAGE(contains(error, archive + " names a zipped nodelist"), error);
    CHECK_MESSAGE(contains(error, "tmpdir has to name one"), error);
}

TEST_CASE("a tmpdir with no zipped nodelist under it is left unmade [nodelist]") {
    // The directory is made where an archive is unpacked and nowhere else: a
    // config naming one it never needs should leave nothing on the disk.
    test::TempDir dir;
    const std::string temporary = dir.path("tmp");

    nodelist::NodelistSources sources(temporary);
    static_cast<void>(amberedit::test::valueOf(
        sources.read(test::projectPath("testdata/nodelist/Z2DAILY.225"), "")));
    CHECK_FALSE(fs::exists(temporary));
}

TEST_CASE("a nodelist name is generalized into the pattern it is one of [nodelist]") {
    // A day number becomes the sentinel that stands for whichever day is
    // newest, which is what a config wants written where a user has pointed at
    // the nodelist that happens to be there today.
    CHECK(nodelist::generalizedSpec("/ftn/nodelist/z2daily.255") ==
          "/ftn/nodelist/z2daily.999");
    CHECK(nodelist::generalizedSpec("z2daily.001") == "z2daily.999");
    CHECK(nodelist::generalizedSpec("z2daily.366") == "z2daily.999");

    // And an archive counter becomes the archive sentinel, in the case it was
    // written in: the two spellings are both used, and a path that came back in
    // the other one would read like a mistake.
    CHECK(nodelist::generalizedSpec("/ftn/z2pnt.z56") == "/ftn/z2pnt.z99");
    CHECK(nodelist::generalizedSpec("/ftn/Z2PNT.Z07") == "/ftn/Z2PNT.Z99");

    // Everything else is a name, and a name is what it stays.
    CHECK(nodelist::generalizedSpec("/ftn/nodelist.ndl") == "/ftn/nodelist.ndl");
    CHECK(nodelist::generalizedSpec("/ftn/z2daily.367") == "/ftn/z2daily.367");
    CHECK(nodelist::generalizedSpec("/ftn/z2daily.000") == "/ftn/z2daily.000");
    CHECK(nodelist::generalizedSpec("/ftn/z2pnt.z00") == "/ftn/z2pnt.z00");
    CHECK(nodelist::generalizedSpec("/ftn/nodelist.zip") == "/ftn/nodelist.zip");
    CHECK(nodelist::generalizedSpec("/ftn/NODELIST") == "/ftn/NODELIST");
    CHECK(nodelist::generalizedSpec("") == "");

    // A dot in a directory name is not an extension.
    CHECK(nodelist::generalizedSpec("/ftn/v3.4/nodelist") == "/ftn/v3.4/nodelist");

    // Saying it twice says the same thing, which is what lets it be run over a
    // path that is already a pattern.
    CHECK(nodelist::generalizedSpec("/ftn/z2daily.999") == "/ftn/z2daily.999");
    CHECK(nodelist::generalizedSpec("/ftn/z2pnt.Z99") == "/ftn/z2pnt.Z99");
}
