#include "nodelist/nodelist_parser.hpp"

#include <doctest/doctest.h>

#include "config/text_util.hpp"
#include "test_paths.hpp"
#include "test_strings.hpp"

using namespace amberedit;
using nodelist::NodeKeyword;

namespace {

const nodelist::NodeEntry* at(const nodelist::ParseResult& parsed,
                              const std::string& address) {
    const auto wanted = domain::FtnAddress::parse(address);
    REQUIRE(wanted);
    for (const auto& entry : parsed.entries) {
        if (entry.address == *wanted) return &entry;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("a nodelist is addressed by the Zone, Region and Host lines above it "
          "[nodelist]") {
    const std::string text =
        "Zone,1,North_America,Toronto,Nick_Andre,1-647-847-2083,9600,CM,XX\r\n"
        ",19,FTSC_Administrator,Groton_CT,Andrew_Leary,1-860-446-6118,33600,H16\r\n"
        ";\r\n"
        "Region,10,Calif-Nevada,Aptos_CA,Kurt_Weiske,-Unpublished-,300,CM\r\n"
        ",1,Region_10_REC,Aptos_CA,Kurt_Weiske,-Unpublished-,300\r\n"
        ";\r\n"
        "Host,102,SoCalNet,Los_Angeles_CA,Lee_Green,-Unpublished-,300,CM,XA\r\n"
        "Hub,400,A_Hub,Nowhere,Nobody,-Unpublished-,300\r\n"
        "Down,127,NOMAD,Torrance_CA,Bradley_Thornton,-Unpublished-,300\r\n"
        "Pvt,401,Techware,Los_Angeles_CA,Lee_Green,-Unpublished-,300\r\n";

    const auto parsed = nodelist::parseNodelist(text);
    CHECK(parsed.warnings.empty());
    CHECK_FALSE(parsed.pointList);
    CHECK(parsed.pointCount == 0);
    REQUIRE(parsed.entries.size() == 8);

    // A zone's own entry is zone:zone/0, and its nodes are in that net.
    REQUIRE(at(parsed, "1:1/0") != nullptr);
    CHECK(at(parsed, "1:1/0")->keyword == NodeKeyword::Zone);
    REQUIRE(at(parsed, "1:1/19") != nullptr);
    CHECK(at(parsed, "1:1/19")->sysop == "Andrew Leary");

    // Region and Host each set the net the lines under them are in.
    REQUIRE(at(parsed, "1:10/0") != nullptr);
    CHECK(at(parsed, "1:10/0")->keyword == NodeKeyword::Region);
    REQUIRE(at(parsed, "1:10/1") != nullptr);
    REQUIRE(at(parsed, "1:102/0") != nullptr);
    CHECK(at(parsed, "1:102/0")->keyword == NodeKeyword::Host);

    REQUIRE(at(parsed, "1:102/400") != nullptr);
    CHECK(at(parsed, "1:102/400")->keyword == NodeKeyword::Hub);
    REQUIRE(at(parsed, "1:102/127") != nullptr);
    CHECK(at(parsed, "1:102/127")->keyword == NodeKeyword::Down);
    REQUIRE(at(parsed, "1:102/401") != nullptr);
    CHECK(at(parsed, "1:102/401")->keyword == NodeKeyword::Pvt);
}

TEST_CASE("the fields of a node are the line's, with the underscores read as spaces "
          "[nodelist]") {
    const std::string text =
        "Zone,2,Europe,Somewhere,Nobody,-Unpublished-,300\r\n"
        "Host,221,Finland,Ylojarvi,Tommi_Koivula,-Unpublished-,300\r\n"
        ",6,MBSE_BBS,Ylojarvi_Finland,Tommi_Koivula,-Unpublished-,33600,CM,XX,"
        "INA:bbs.example.org,IBN:24555,PING\r\n";

    const auto parsed = nodelist::parseNodelist(text);
    const auto* node = at(parsed, "2:221/6");
    REQUIRE(node != nullptr);
    CHECK(node->keyword == NodeKeyword::Node);
    CHECK(node->system == "MBSE BBS");
    CHECK(node->location == "Ylojarvi Finland");
    CHECK(node->sysop == "Tommi Koivula");
    CHECK(node->phone == "-Unpublished-");
    CHECK(node->speed == 33600);
    CHECK(node->flags == "CM,XX,INA:bbs.example.org,IBN:24555,PING");

    CHECK(node->hasFlag("CM"));
    CHECK(node->hasFlag("ibn"));
    CHECK_FALSE(node->hasFlag("IB"));
    CHECK(node->flagValue("INA") == std::string("bbs.example.org"));
    CHECK_FALSE(node->flagValue("CM"));
    CHECK_FALSE(node->flagValue("IFC"));

    // The line comes back as it was written, underscores and all — which is
    // what a reader shows when it is asked for the node's line.
    CHECK(node->toLine() ==
          ",6,MBSE_BBS,Ylojarvi_Finland,Tommi_Koivula,-Unpublished-,33600,CM,XX,"
          "INA:bbs.example.org,IBN:24555,PING");
}

TEST_CASE("a Boss line makes the lines under it points of that node [nodelist]") {
    const std::string text =
        "Boss,2:221/6\r\n"
        ",66,FPoint,Ylojarvi,Tommi_Koivula,-Unpublished-,300,PING\r\n"
        ";\r\n"
        "Boss,2:240/2188\r\n"
        "Pvt,1,Kruemel_Boks!,Boeblingen,Christian_von_Busse,-Unpublished-,300\r\n"
        "Pvt,13,Kruemel_Boks!_P13,Ober-Ramstadt,Joerg_Walther,-Unpublished-,300\r\n";

    const auto parsed = nodelist::parseNodelist(text);
    CHECK(parsed.warnings.empty());
    CHECK(parsed.pointList);
    CHECK(parsed.pointCount == 3);
    REQUIRE(parsed.entries.size() == 3);

    REQUIRE(at(parsed, "2:221/6.66") != nullptr);
    CHECK(at(parsed, "2:221/6.66")->sysop == "Tommi Koivula");
    REQUIRE(at(parsed, "2:240/2188.1") != nullptr);
    CHECK(at(parsed, "2:240/2188.1")->system == "Kruemel Boks!");
    REQUIRE(at(parsed, "2:240/2188.13") != nullptr);
    CHECK(at(parsed, "2:240/2188.13")->sysop == "Joerg Walther");

    // A point's line names its point number, not its node's.
    CHECK(at(parsed, "2:240/2188.13")->toLine() ==
          "Pvt,13,Kruemel_Boks!_P13,Ober-Ramstadt,Joerg_Walther,-Unpublished-,300");
}

TEST_CASE("a Point line makes a point of the node above it [nodelist]") {
    const std::string text =
        "Zone,2,Europe,Somewhere,Nobody,-Unpublished-,300\r\n"
        "Host,5020,Russia,Moscow,Nobody,-Unpublished-,300\r\n"
        ",999,A_Node,Moscow,Some_Sysop,-Unpublished-,300\r\n"
        "Point,1,A_Point,Moscow,Point_Op,-Unpublished-,300\r\n"
        ",1000,Another_Node,Moscow,Other_Sysop,-Unpublished-,300\r\n";

    const auto parsed = nodelist::parseNodelist(text);
    CHECK(parsed.warnings.empty());
    CHECK(parsed.pointCount == 1);
    REQUIRE(at(parsed, "2:5020/999.1") != nullptr);
    CHECK(at(parsed, "2:5020/999.1")->sysop == "Point Op");
    // The point does not carry the node along with it: the line after it is a
    // node of the net again.
    REQUIRE(at(parsed, "2:5020/1000") != nullptr);
}

TEST_CASE("a line that cannot be read is named and left out [nodelist]") {
    const std::string text =
        ",19,An_Orphan,Nowhere,Nobody,-Unpublished-,300\r\n"
        "Zone,2,Europe,Somewhere,Nobody,-Unpublished-,300\r\n"
        "Nonsense,7,Something,Nowhere,Nobody,-Unpublished-,300\r\n"
        ",xx,Not_A_Number,Nowhere,Nobody,-Unpublished-,300\r\n"
        ",7,Fine,Nowhere,Nobody,-Unpublished-,fast\r\n";

    const auto parsed = nodelist::parseNodelist(text);
    REQUIRE(parsed.entries.size() == 2);
    REQUIRE(parsed.warnings.size() == 4);
    CHECK(parsed.warnings[0].line == 1);
    CHECK(parsed.warnings[1].line == 3);
    CHECK(parsed.warnings[2].line == 4);
    CHECK(parsed.warnings[3].line == 5);

    // A baud rate that is not one keeps the node: the address and the sysop are
    // what anybody was looking for, and the field is left at zero.
    REQUIRE(at(parsed, "2:2/7") != nullptr);
    CHECK(at(parsed, "2:2/7")->speed == 0);
}

TEST_CASE("the real nodelists in testdata parse [nodelist]") {
    const auto text = test::valueOf(
        config::text::readFile(test::projectPath("testdata/nodelist/Z2DAILY.225")));
    const auto parsed = nodelist::parseNodelist(text);

    CHECK_FALSE(parsed.pointList);
    CHECK(parsed.pointCount == 0);
    // A day's Z2DAILY is over a thousand nodes; the exact number changes with
    // the file, so what is checked is that the whole of it was read.
    CHECK(parsed.entries.size() > 1000);
    CHECK(parsed.warnings.empty());

    const auto* zone = at(parsed, "2:2/0");
    REQUIRE(zone != nullptr);
    CHECK(zone->keyword == NodeKeyword::Zone);

    const auto* node = at(parsed, "2:221/6");
    REQUIRE(node != nullptr);
    CHECK(node->sysop == "Tommi Koivula");
}
