#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "domain/message.hpp"
#include "msgbase/ftn_msgbase.hpp"
#include "support/error.hpp"
#include "temp_dir.hpp"
#include "temp_squish_base.hpp"
#include "test_strings.hpp"

using amberedit::ErrorPtr;
using amberedit::MsgBaseError;
using amberedit::domain::AreaConfig;
using amberedit::domain::MessageDraft;
using amberedit::domain::MsgBaseType;
using amberedit::msgbase::FtnMsgBase;
using amberedit::test::TempDir;
using amberedit::test::TempSquishBase;
using amberedit::test::contains;
using amberedit::test::errorOf;
using amberedit::test::valueOf;

namespace fs = std::filesystem;

namespace {

/// Whether anything stands at `path`. The error_code overload throughout: one
/// of the tests asks about a path whose parent is a regular file, and what that
/// answers is an errno rather than an exception worth throwing.
bool present(const std::string& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

/// Which kind of failure an answer carries, for the tests that turn on it
/// rather than on the sentence: whether a base is absent or half there is what
/// decides whether one is created, and the two say different things to a
/// person as well.
MsgBaseError::Kind kindOf(const tl::expected<void, ErrorPtr>& result) {
    REQUIRE_FALSE(result.has_value());
    const auto* why = dynamic_cast<const MsgBaseError*>(result.error().get());
    REQUIRE(why != nullptr);
    return why->kind();
}

AreaConfig areaAt(const std::string& path, MsgBaseType type) {
    AreaConfig area;
    area.tag = "new.area";
    area.path = path;
    area.type = type;
    return area;
}

/// A message to put into a base that has just been made, which is the whole
/// point of making one: an area is created so that a first message can go in.
MessageDraft firstMessage() {
    MessageDraft draft;
    draft.from = "Yegor Gluhov";
    draft.to = "All";
    draft.subject = "The first message";
    draft.origAddr = *amberedit::domain::FtnAddress::parse("192:168/2");
    draft.destAddr = *amberedit::domain::FtnAddress::parse("192:168/2");
    draft.charset = "CP866";
    draft.kludges = {"MSGID: 192:168/2 68a1b2c3", "CHRS: CP866 2"};
    draft.lines = {"Hello from a base nothing had written to."};
    return draft;
}

/// Makes the base, opens it, writes one message and reads it back through a
/// second open — the round trip every format has to pass alike.
void checkCreatedBaseTakesAMessage(const AreaConfig& area) {
    FtnMsgBase created("CP866");
    REQUIRE(created.create(area).has_value());
    // Creating leaves the base on disk and the adapter closed on it: what is
    // read afterwards is the base as it stands, not as create() imagined it.
    CHECK_FALSE(created.isOpen());
    CHECK(FtnMsgBase::probeType(area.path) == area.type);
    CHECK_FALSE(FtnMsgBase::isAbsent(area));

    FtnMsgBase base("CP866");
    REQUIRE(base.open(area).has_value());
    CHECK(base.count() == 0);
    REQUIRE(valueOf(base.write(firstMessage())) == 1);
    base.close();

    FtnMsgBase again("CP866");
    REQUIRE(again.open(area).has_value());
    REQUIRE(again.count() == 1);
    CHECK(again.header(1).subject == "The first message");
    CHECK(again.header(1).from == "Yegor Gluhov");
    // A UID of its own, which is what a lastread mark will hold.
    CHECK(again.uidOf(1) != 0);
}

/// The base made and a message put into it, so that what is left when a file of
/// it is taken away is a base that holds something.
void fillBase(const AreaConfig& area) {
    REQUIRE(FtnMsgBase("CP866").create(area).has_value());
    FtnMsgBase base("CP866");
    REQUIRE(base.open(area).has_value());
    REQUIRE(valueOf(base.write(firstMessage())) == 1);
}

/// Every byte of a file, for the checks that what a half-made base still had is
/// left exactly as it stood.
std::string bytesOf(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

/// The base made, the files in `gone` taken away again, and the area opened on
/// what is left: a base interrupted on its way into being, which opening is
/// expected to finish rather than refuse.
void checkHalfMadeBaseIsCompleted(const AreaConfig& area,
                                  const std::vector<std::string>& gone) {
    REQUIRE(FtnMsgBase("CP866").create(area).has_value());
    for (const std::string& file : gone) {
        REQUIRE(present(file));
        fs::remove(file);
    }

    FtnMsgBase base("CP866");
    const auto opened = base.open(area);
    REQUIRE_MESSAGE(opened.has_value(), errorOf(opened));
    // Empty, which is what it was: nothing was put back that holds anything.
    CHECK(base.count() == 0);
    for (const std::string& file : gone) CHECK(present(file));

    // And a base to write into, which is the whole point of completing it.
    REQUIRE(valueOf(base.write(firstMessage())) == 1);
    base.close();

    FtnMsgBase again("CP866");
    REQUIRE(again.open(area).has_value());
    REQUIRE(again.count() == 1);
    CHECK(again.header(1).subject == "The first message");
}

}  // namespace

TEST_CASE("A created Squish base opens empty and takes a message [create]") {
    TempDir dir;
    checkCreatedBaseTakesAMessage(areaAt(dir.path("fresh"), MsgBaseType::Squish));
}

TEST_CASE("A created JAM base opens empty and takes a message [create][jam]") {
    TempDir dir;
    checkCreatedBaseTakesAMessage(areaAt(dir.path("fresh"), MsgBaseType::Jam));
}

TEST_CASE("A created Fido *.msg base opens empty and takes a message "
          "[create][opus]") {
    TempDir dir;
    checkCreatedBaseTakesAMessage(areaAt(dir.path("fresh"), MsgBaseType::Opus));
}

TEST_CASE("Creating a base makes the files its format is read through "
          "[create]") {
    TempDir dir;

    SUBCASE("Squish") {
        const std::string path = dir.path("fresh");
        REQUIRE(
            FtnMsgBase("CP866").create(areaAt(path, MsgBaseType::Squish)).has_value());
        CHECK(fs::exists(path + ".sqd"));
        CHECK(fs::exists(path + ".sqi"));
        // The index holds one record per message, and there are none.
        CHECK(fs::file_size(path + ".sqi") == 0);
    }
    SUBCASE("JAM") {
        const std::string path = dir.path("fresh");
        REQUIRE(FtnMsgBase("CP866").create(areaAt(path, MsgBaseType::Jam)).has_value());
        CHECK(fs::exists(path + ".jhr"));
        CHECK(fs::exists(path + ".jdx"));
        CHECK(fs::exists(path + ".jdt"));
        CHECK(fs::file_size(path + ".jdx") == 0);
        CHECK(fs::file_size(path + ".jdt") == 0);
    }
    SUBCASE("Fido *.msg") {
        const std::string path = dir.path("fresh");
        REQUIRE(FtnMsgBase("CP866").create(areaAt(path, MsgBaseType::Opus)).has_value());
        CHECK(fs::is_directory(path));
    }
}

TEST_CASE("A base that is already there is never created over [create]") {
    // The one thing creating must not do: an area someone is reading is an area
    // with messages in it, and an empty base written over it would take them.
    TempSquishBase existing;
    const AreaConfig area = areaAt(existing.path(), MsgBaseType::Squish);

    CHECK_FALSE(FtnMsgBase::isAbsent(area));

    FtnMsgBase base("CP866");
    const auto made = base.create(area);
    CHECK_FALSE(made.has_value());
    CHECK_FALSE(made.error()->message().empty());

    // And it is still the base it was.
    FtnMsgBase reader("CP866");
    REQUIRE(reader.open(area).has_value());
    CHECK(reader.count() > 0);
}

TEST_CASE("A base of another format under the same name is another area's "
          "[create]") {
    // Two formats share a base name easily — a tosser moving an area from one
    // to the other leaves both sets of files, and a config may name the same
    // path for two areas. What stands at the path in somebody else's format
    // says nothing about this area: it neither opens it nor stands between the
    // user and creating it.
    TempDir dir;
    const std::string path = dir.path("shared");
    REQUIRE(FtnMsgBase("CP866").create(areaAt(path, MsgBaseType::Squish)).has_value());
    const AreaConfig jam = areaAt(path, MsgBaseType::Jam);

    // The probe answers for the path and finds the Squish base, which is why it
    // is not what a stated type is decided by.
    CHECK(FtnMsgBase::probeType(path) == MsgBaseType::Squish);
    CHECK(FtnMsgBase::isAbsent(jam));

    FtnMsgBase base("CP866");
    const auto opened = base.open(jam);
    CHECK(kindOf(opened) == MsgBaseError::Kind::Absent);

    // And it is made beside the other one, and takes a message. Written out
    // rather than through checkCreatedBaseTakesAMessage(), whose premise is a
    // path the probe answers for — which is exactly what this path is not.
    REQUIRE(FtnMsgBase("CP866").create(jam).has_value());
    CHECK_FALSE(FtnMsgBase::isAbsent(jam));
    {
        FtnMsgBase made("CP866");
        REQUIRE(made.open(jam).has_value());
        CHECK(made.count() == 0);
        REQUIRE(valueOf(made.write(firstMessage())) == 1);
    }
    FtnMsgBase again("CP866");
    REQUIRE(again.open(jam).has_value());
    REQUIRE(again.count() == 1);
    CHECK(again.header(1).subject == "The first message");
    again.close();

    // Which left the Squish base where it was.
    CHECK(present(path + ".sqd"));
    CHECK(present(path + ".sqi"));

    // The same the other way round: the Squish base opens with a JAM one now
    // beside it.
    FtnMsgBase squish("CP866");
    CHECK(squish.open(areaAt(path, MsgBaseType::Squish)).has_value());
}

TEST_CASE("A base short of a file of its own and holding messages is not opened "
          "and not created over [create]") {
    // The half-there base that has something in it: an index a script took for
    // rebuildable and swept, a file lost off a disk. It is not a base to read,
    // and above all it is not one to create over or to complete — the message
    // it holds would go with either.
    TempDir dir;

    SUBCASE("JAM without its index") {
        const std::string path = dir.path("fresh");
        const AreaConfig area = areaAt(path, MsgBaseType::Jam);
        fillBase(area);
        fs::remove(path + ".jdx");

        FtnMsgBase base("CP866");
        const auto opened = base.open(area);
        CHECK(kindOf(opened) == MsgBaseError::Kind::Incomplete);
        // Naming the file, which is the half the user acts on.
        CHECK_MESSAGE(contains(errorOf(opened), path + ".jdx"), errorOf(opened));
        // And the file is still not there: a base holding a message is not one
        // an index is invented for.
        CHECK_FALSE(present(path + ".jdx"));

        CHECK_FALSE(FtnMsgBase::isAbsent(area));
        CHECK_FALSE(FtnMsgBase("CP866").create(area).has_value());
        CHECK(present(path + ".jhr"));
        CHECK(present(path + ".jdt"));
    }
    SUBCASE("JAM without its text file") {
        const std::string path = dir.path("fresh");
        const AreaConfig area = areaAt(path, MsgBaseType::Jam);
        fillBase(area);
        fs::remove(path + ".jdt");

        FtnMsgBase base("CP866");
        const auto opened = base.open(area);
        CHECK(kindOf(opened) == MsgBaseError::Kind::Incomplete);
        CHECK_MESSAGE(contains(errorOf(opened), path + ".jdt"), errorOf(opened));
        CHECK_FALSE(present(path + ".jdt"));
    }
    SUBCASE("Squish without its index") {
        const std::string path = dir.path("fresh");
        const AreaConfig area = areaAt(path, MsgBaseType::Squish);
        fillBase(area);
        fs::remove(path + ".sqi");

        FtnMsgBase base("CP866");
        const auto opened = base.open(area);
        CHECK(kindOf(opened) == MsgBaseError::Kind::Incomplete);
        CHECK_MESSAGE(contains(errorOf(opened), path + ".sqi"), errorOf(opened));
        CHECK_FALSE(present(path + ".sqi"));

        CHECK_FALSE(FtnMsgBase::isAbsent(area));
        CHECK_FALSE(FtnMsgBase("CP866").create(area).has_value());
        CHECK(present(path + ".sqd"));
    }
}

TEST_CASE("A base short of a file of its own and holding nothing is completed "
          "as it opens [create]") {
    // The other half-there base, and the ordinary one: a tosser stopped between
    // two of the files it makes, a disk that filled between one create and the
    // next. Nothing was lost with the file that is gone — what is standing is a
    // header and no messages behind it — so the area is finished here and
    // opened, rather than refused as an area with a piece missing.
    TempDir dir;

    SUBCASE("Squish without its index") {
        const std::string path = dir.path("fresh");
        checkHalfMadeBaseIsCompleted(areaAt(path, MsgBaseType::Squish),
                                     {path + ".sqi"});
    }
    SUBCASE("Squish without the file it is found by") {
        const std::string path = dir.path("fresh");
        checkHalfMadeBaseIsCompleted(areaAt(path, MsgBaseType::Squish),
                                     {path + ".sqd"});
    }
    SUBCASE("JAM without its index") {
        const std::string path = dir.path("fresh");
        checkHalfMadeBaseIsCompleted(areaAt(path, MsgBaseType::Jam), {path + ".jdx"});
    }
    SUBCASE("JAM without its text file") {
        const std::string path = dir.path("fresh");
        checkHalfMadeBaseIsCompleted(areaAt(path, MsgBaseType::Jam), {path + ".jdt"});
    }
    SUBCASE("JAM with its headers alone") {
        const std::string path = dir.path("fresh");
        checkHalfMadeBaseIsCompleted(areaAt(path, MsgBaseType::Jam),
                                     {path + ".jdx", path + ".jdt"});
    }
    SUBCASE("JAM without the file it is found by") {
        const std::string path = dir.path("fresh");
        checkHalfMadeBaseIsCompleted(areaAt(path, MsgBaseType::Jam), {path + ".jhr"});
    }
    SUBCASE("JAM with its text file alone") {
        const std::string path = dir.path("fresh");
        checkHalfMadeBaseIsCompleted(areaAt(path, MsgBaseType::Jam),
                                     {path + ".jhr", path + ".jdx"});
    }
}

TEST_CASE("Completing a half-made base writes over nothing it found [create]") {
    // The file that is there is the base's own, whatever it holds: the info
    // block carries the date the area came into being and the number its first
    // message will take, and completing the base beside it must leave it byte
    // for byte as it stood.
    TempDir dir;
    const std::string path = dir.path("fresh");
    const AreaConfig area = areaAt(path, MsgBaseType::Jam);
    REQUIRE(FtnMsgBase("CP866").create(area).has_value());
    const std::string headers = bytesOf(path + ".jhr");
    REQUIRE(headers.size() == 1024);
    fs::remove(path + ".jdx");

    FtnMsgBase base("CP866");
    REQUIRE(base.open(area).has_value());
    base.close();

    CHECK(bytesOf(path + ".jhr") == headers);
    CHECK(fs::file_size(path + ".jdx") == 0);
}

TEST_CASE("A file of a half-made base that does not read as an empty one is "
          "left alone [create]") {
    // Empty is the state creating a base leaves a file in, and nothing else:
    // the size of a header and a header in it. A file of zeroes is what a
    // tosser that died in the middle of writing one leaves, and it is the size
    // of an empty base without being one — completing the area beside it would
    // make a base out of a file nothing can say anything about.
    TempDir dir;

    SUBCASE("a .sqd of zeroes") {
        const std::string path = dir.path("fresh");
        std::ofstream(path + ".sqd", std::ios::binary) << std::string(256, '\0');

        FtnMsgBase base("CP866");
        const auto opened = base.open(areaAt(path, MsgBaseType::Squish));
        CHECK(kindOf(opened) == MsgBaseError::Kind::Incomplete);
        CHECK_FALSE(present(path + ".sqi"));
    }
    SUBCASE("a .jhr of zeroes") {
        const std::string path = dir.path("fresh");
        std::ofstream(path + ".jhr", std::ios::binary) << std::string(1024, '\0');

        FtnMsgBase base("CP866");
        const auto opened = base.open(areaAt(path, MsgBaseType::Jam));
        CHECK(kindOf(opened) == MsgBaseError::Kind::Incomplete);
        CHECK_FALSE(present(path + ".jdx"));
        CHECK_FALSE(present(path + ".jdt"));
    }
    SUBCASE("a .jdt with bytes in it") {
        // The headers are an empty base's and the text file is not empty: the
        // messages whose text that is are what the missing .jdx named, and
        // handing the area a new empty one would lose them for good.
        const std::string path = dir.path("fresh");
        const AreaConfig area = areaAt(path, MsgBaseType::Jam);
        REQUIRE(FtnMsgBase("CP866").create(area).has_value());
        fs::remove(path + ".jdx");
        std::ofstream(path + ".jdt", std::ios::binary | std::ios::app) << "text";

        FtnMsgBase base("CP866");
        const auto opened = base.open(area);
        CHECK(kindOf(opened) == MsgBaseError::Kind::Incomplete);
        CHECK_FALSE(present(path + ".jdx"));
    }
}

TEST_CASE("What a base that lost the file it is found by leaves is kept "
          "[create][jam]") {
    // The other half of the same state, and the one that used to end in an
    // empty base: nothing is *found* at the path, because a JAM base is found
    // by its .jhr, while the messages are sitting in the .jdt beside it.
    TempDir dir;
    const std::string path = dir.path("fresh");
    const AreaConfig area = areaAt(path, MsgBaseType::Jam);
    REQUIRE(FtnMsgBase("CP866").create(area).has_value());
    {
        FtnMsgBase base("CP866");
        REQUIRE(base.open(area).has_value());
        REQUIRE(valueOf(base.write(firstMessage())) == 1);
    }
    const auto textSize = fs::file_size(path + ".jdt");
    REQUIRE(textSize > 0);

    fs::remove(path + ".jhr");
    CHECK(FtnMsgBase::probeType(path) == MsgBaseType::Unknown);
    CHECK_FALSE(FtnMsgBase::isAbsent(area));

    FtnMsgBase base("CP866");
    const auto opened = base.open(area);
    CHECK(kindOf(opened) == MsgBaseError::Kind::Incomplete);
    CHECK_MESSAGE(contains(errorOf(opened), path + ".jhr"), errorOf(opened));

    CHECK_FALSE(FtnMsgBase("CP866").create(area).has_value());
    // The whole point of the refusal: the message is where it was.
    CHECK(fs::file_size(path + ".jdt") == textSize);
    CHECK(present(path + ".jdx"));
}

TEST_CASE("An area with no stated type is not one to create [create]") {
    // Nothing is on disk to work the format out from, and guessing at one would
    // write a base of the wrong kind that a tosser then refuses.
    TempDir dir;
    const AreaConfig area = areaAt(dir.path("fresh"), MsgBaseType::Unknown);

    CHECK_FALSE(FtnMsgBase::isAbsent(area));
    FtnMsgBase base("CP866");
    const auto made = base.create(area);
    CHECK_FALSE(made.has_value());
    CHECK_FALSE(made.error()->message().empty());
}

TEST_CASE("A passthrough area has no base to create [create]") {
    AreaConfig area;
    area.tag = "pass.through";
    area.type = MsgBaseType::Passthrough;

    CHECK_FALSE(FtnMsgBase::isAbsent(area));
    FtnMsgBase base("CP866");
    CHECK_FALSE(base.create(area).has_value());
}

TEST_CASE("A base that cannot be created says so and leaves nothing behind "
          "[create]") {
    TempDir dir;
    // A regular file where a directory would have to be: every format has to
    // put something under it, so none of them can.
    const std::string blocked = dir.path("a-file");
    { std::ofstream(blocked) << "not a directory"; }

    const std::string path = blocked + "/fresh";

    SUBCASE("Squish") {
        FtnMsgBase base("CP866");
        const auto made = base.create(areaAt(path, MsgBaseType::Squish));
        CHECK_FALSE(made.has_value());
        CHECK_FALSE(made.error()->message().empty());
        CHECK_FALSE(present(path + ".sqd"));
        CHECK_FALSE(present(path + ".sqi"));
    }
    SUBCASE("JAM") {
        FtnMsgBase base("CP866");
        const auto made = base.create(areaAt(path, MsgBaseType::Jam));
        CHECK_FALSE(made.has_value());
        CHECK_FALSE(made.error()->message().empty());
        CHECK_FALSE(present(path + ".jhr"));
        CHECK_FALSE(present(path + ".jdx"));
        CHECK_FALSE(present(path + ".jdt"));
    }
    SUBCASE("Fido *.msg") {
        FtnMsgBase base("CP866");
        const auto made = base.create(areaAt(path, MsgBaseType::Opus));
        CHECK_FALSE(made.has_value());
        CHECK_FALSE(made.error()->message().empty());
        CHECK_FALSE(present(path));
    }
}
