#include <doctest/doctest.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <filesystem>
#include <string>

#include "domain/area.hpp"
#include "domain/message.hpp"
#include "msgbase/ftn_msgbase.hpp"
#include "temp_dir.hpp"
#include "test_strings.hpp"

using amberedit::domain::AreaConfig;
using amberedit::domain::MessageDraft;
using amberedit::domain::MsgBaseType;
using amberedit::msgbase::FtnMsgBase;
using amberedit::test::TempDir;

namespace fs = std::filesystem;

namespace {

/// Whether taking the write bits away really takes writing away here.
///
/// Root is refused nothing, and Windows has no such bits — `fs::permissions`
/// cannot clear them and `fs::status` reports them granted to everybody. A test
/// that turns on them has nothing to prove on either, and says so rather than
/// passing quietly.
bool readOnlyBites() {
#ifdef _WIN32
    return false;
#else
    return ::geteuid() != 0;
#endif
}

AreaConfig areaAt(const std::string& path, MsgBaseType type) {
    AreaConfig area;
    area.tag = "read.only";
    area.path = path;
    area.type = type;
    return area;
}

/// Takes the write bit off everything the base is made of, and off the base
/// itself where it is a directory.
void makeReadOnly(const std::string& path, MsgBaseType type) {
    const auto strip = [](const fs::path& file) {
        fs::permissions(file, fs::perms::owner_write | fs::perms::group_write |
                                  fs::perms::others_write,
                        fs::perm_options::remove);
    };
    switch (type) {
        case MsgBaseType::Squish:
            strip(path + ".sqd");
            strip(path + ".sqi");
            break;
        case MsgBaseType::Jam:
            strip(path + ".jhr");
            strip(path + ".jdx");
            strip(path + ".jdt");
            break;
        default: strip(path); break;
    }
}

/// Put back, so that the temporary directory can be cleared away.
void makeWritableAgain(const std::string& path, MsgBaseType type) {
    const auto grant = [](const fs::path& file) {
        std::error_code ec;
        fs::permissions(file, fs::perms::owner_write, fs::perm_options::add, ec);
    };
    switch (type) {
        case MsgBaseType::Squish:
            grant(path + ".sqd");
            grant(path + ".sqi");
            break;
        case MsgBaseType::Jam:
            grant(path + ".jhr");
            grant(path + ".jdx");
            grant(path + ".jdt");
            break;
        default: grant(path); break;
    }
}

MessageDraft aMessage() {
    MessageDraft draft;
    draft.from = "Yegor Gluhov";
    draft.to = "All";
    draft.subject = "On a spool of somebody else's";
    draft.origAddr = *amberedit::domain::FtnAddress::parse("192:168/2");
    draft.destAddr = *amberedit::domain::FtnAddress::parse("192:168/2");
    draft.charset = "CP866";
    draft.kludges = {"MSGID: 192:168/2 68a1b2c3", "CHRS: CP866 2"};
    draft.lines = {"Read here, written elsewhere."};
    return draft;
}

/// The base opens, reads and refuses to be written — all three, because the
/// refusing is only worth anything if the reading still works. An area on
/// somebody else's spool is read every day.
void checkReadOnlyBaseReadsAndRefuses(MsgBaseType type) {
    TempDir dir;
    const std::string path = dir.path("spool");
    const AreaConfig area = areaAt(path, type);
    REQUIRE(FtnMsgBase("CP866").create(area).has_value());

    {
        FtnMsgBase writer("CP866");
        REQUIRE(writer.open(area).has_value());
        CHECK(writer.isWritable());
        REQUIRE(amberedit::test::valueOf(writer.write(aMessage())) == 1);
    }

    makeReadOnly(path, type);

    FtnMsgBase base("CP866");
    const auto opened = base.open(area);
    CHECK_MESSAGE(opened.has_value(), amberedit::test::errorOf(opened));
    // Read in full: this is an area somebody reads every day, and the whole
    // point of refusing the write is that the reading goes on.
    REQUIRE(base.count() == 1);
    CHECK(base.header(1).subject == "On a spool of somebody else's");
    CHECK_FALSE(base.isWritable());

    // And the write it was asked about really is refused, with something to
    // say: it is what the editor puts in the box when it gets that far.
    const auto written = base.write(aMessage());
    CHECK_FALSE(written.has_value());
    CHECK_FALSE(written.error()->message().empty());
    CHECK(base.count() == 1);
    base.close();

    makeWritableAgain(path, type);
}

}  // namespace

TEST_CASE("A base that cannot be written is read and says it cannot [readonly]") {
    if (!readOnlyBites()) {
        MESSAGE("mode bits do not bite here: root, or Windows");
        return;
    }

    SUBCASE("Squish") { checkReadOnlyBaseReadsAndRefuses(MsgBaseType::Squish); }
    SUBCASE("JAM") { checkReadOnlyBaseReadsAndRefuses(MsgBaseType::Jam); }
    SUBCASE("Fido *.msg") { checkReadOnlyBaseReadsAndRefuses(MsgBaseType::Opus); }
}

TEST_CASE("An area nothing is open on is not one to write into [readonly]") {
    // The question is asked wherever a message might be written, and an area
    // that would not open is one of the answers it has to have.
    FtnMsgBase base("CP866");
    CHECK_FALSE(base.isWritable());
}
