#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "comma_locale.hpp"
#include "prism/hdr.hpp"
#include "prism/process.hpp"
#include "prism/recipe.hpp"
#include "prism/utf8_path.hpp"

// #102: the headroom in recipes ([hdr], format 3) and the parsing behind --headroom.

namespace fs = std::filesystem;

namespace {

const prism::RecipeWriteOptions bare{false};

std::vector<prism::RecipeIssue> issues_in(const std::string& text) {
    try {
        (void)prism::parse_recipe(text, "test.prism");
    } catch (const prism::RecipeError& e) {
        return e.issues();
    }
    return {};
}

// `name` is UTF-8 text.
fs::path temp_path(const std::string& name) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
    return fs::path(testing::TempDir()) /
           prism::path_from_utf8("prism_headroom_" + std::string(test->name()) + "_" + name);
}

std::string read_bytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

}  // namespace

// --- parse_headroom (--headroom) ----------------------------------------------------------------

TEST(ParseHeadroom, NumbersFromZeroToFour) {
    EXPECT_EQ(prism::parse_headroom("2"), 2.0f);
    EXPECT_EQ(prism::parse_headroom("+2.5"), 2.5f);
    EXPECT_EQ(prism::parse_headroom("0"), 0.0f);
    EXPECT_EQ(prism::parse_headroom("4"), 4.0f);
    EXPECT_EQ(prism::parse_headroom("4.0"), 4.0f);
    EXPECT_EQ(prism::parse_headroom("0.1"), 0.1f);
    // Rounded to float first, as a recipe reads it: just over 4 in the text is 4.0f.
    EXPECT_EQ(prism::parse_headroom("4.00000001"), 4.0f);
    // -0 with either minus is 0.
    for (const char* zero : {"-0",
                             "\xE2\x88\x92"
                             "0"}) {
        const std::optional<float> v = prism::parse_headroom(zero);
        ASSERT_TRUE(v) << zero;
        EXPECT_EQ(*v, 0.0f) << zero;
    }
}

TEST(ParseHeadroom, AnythingElseIsRefused) {
    for (const char* bad : {"",
                            " ",
                            "4.01",
                            "5",
                            "-0.1",
                            "\xE2\x88\x92"
                            "1",
                            "1,5",
                            "2 ",
                            " 2",
                            "2stops",
                            "two",
                            "inf",
                            "nan",
                            "+",
                            "1e309",
                            "--2",
                            "\xE2\x88\x92",
                            "0x2",
                            "0X1p1",
                            "+0x1",
                            "4.0001"}) {
        EXPECT_FALSE(prism::parse_headroom(bad)) << "'" << bad << "'";
    }
}

TEST(ParseHeadroom, ReadsADotWhateverTheLocale) {
    const CommaLocale comma;
    if (!comma.active()) {
        GTEST_SKIP() << "no locale with a decimal comma on this machine";
    }
    EXPECT_EQ(prism::parse_headroom("1.5"), 1.5f);
    EXPECT_FALSE(prism::parse_headroom("1,5"));
}

// --- reading [hdr]
// --------------------------------------------------------------------------------

TEST(RecipeHdr, AFormat3RecipeReadsItsHeadroom) {
    const prism::Recipe r = prism::parse_recipe("prism_recipe = 3\n\n[hdr]\nheadroom = +2.5\n");
    ASSERT_TRUE(r.hdr);
    EXPECT_EQ(r.hdr->headroom, 2.5f);
    // Unsigned, a real minus on 0, and 0 itself all read as written.
    EXPECT_EQ(prism::parse_recipe("prism_recipe = 3\n[hdr]\nheadroom = 4\n").hdr->headroom, 4.0f);
    EXPECT_EQ(prism::parse_recipe("prism_recipe = 3\n[hdr]\nheadroom = 0\n").hdr->headroom, 0.0f);
    EXPECT_EQ(prism::parse_recipe("prism_recipe = 3\n[hdr]\nheadroom = \xE2\x88\x92"
                                  "0\n")
                  .hdr->headroom,
              0.0f);
    EXPECT_EQ(prism::parse_recipe("prism_recipe = 3\n[hdr]\nheadroom = -0.0\n").hdr->headroom,
              0.0f);
}

TEST(RecipeHdr, OlderFormatsReadAsBefore) {
    // Format 1 (a built-in look as 0.7.1 wrote it) and format 2 (with [white_balance]): no [hdr],
    // and they save as the same version again.
    const std::string v1 =
        "prism_recipe = 1\n\n[exposure]\nstops = +1.0\n\n[curve]\nshape = \"filmic\"\n";
    const prism::Recipe one = prism::parse_recipe(v1);
    EXPECT_FALSE(one.hdr);
    EXPECT_EQ(prism::write_recipe(one, bare), v1);
    EXPECT_FALSE(prism::to_process_settings(one).headroom_stops);

    const std::string v2 = "prism_recipe = 2\n\n[white_balance]\nkelvin = 5000.0\ntint = 0.0\n";
    const prism::Recipe two = prism::parse_recipe(v2);
    EXPECT_FALSE(two.hdr);
    EXPECT_EQ(prism::write_recipe(two, bare), v2);
    EXPECT_FALSE(prism::to_process_settings(two).headroom_stops);
}

TEST(RecipeHdr, NeedsFormat3) {
    const std::vector<prism::RecipeIssue> expected = {
        {3, "[hdr] needs recipe format 3, but this file says prism_recipe = 2"}};
    EXPECT_EQ(issues_in("prism_recipe = 2\n\n[hdr]\nheadroom = +2.0\n"), expected);
    const std::vector<prism::RecipeIssue> in_one = {
        {2, "[hdr] needs recipe format 3, but this file says prism_recipe = 1"}};
    EXPECT_EQ(issues_in("prism_recipe = 1\n[hdr]\nheadroom = +2.0\n"), in_one);
}

TEST(RecipeHdr, BadAndOutOfRangeValues) {
    struct Case {
        const char* value;
        const char* message;
    };
    const Case cases[] = {
        {"4.5", "[hdr] headroom must be from 0 to 4 (it is 4.5)"},
        {"+5", "[hdr] headroom must be from 0 to 4 (it is 5)"},
        {"-0.5",
         "[hdr] headroom must be from 0 to 4 (it is \xE2\x88\x92"
         "0.5)"},
        {"\xE2\x88\x92"
         "1",
         "[hdr] headroom must be from 0 to 4 (it is \xE2\x88\x92"
         "1)"},
    };
    for (const Case& c : cases) {
        const std::vector<prism::RecipeIssue> expected = {{3, c.message}};
        EXPECT_EQ(issues_in("prism_recipe = 3\n[hdr]\nheadroom = " + std::string(c.value) + "\n"),
                  expected)
            << c.value;
    }
    // Not numbers at all: refused (the wording is shared with every other number key).
    for (const char* bad : {"two", "1,5", "\"2\"", "2 stops", "true", "[2]", "inf"}) {
        EXPECT_EQ(
            issues_in("prism_recipe = 3\n[hdr]\nheadroom = " + std::string(bad) + "\n").size(), 1u)
            << bad;
    }
    const std::vector<prism::RecipeIssue> word = {{3, "'two' is not a number"}};
    EXPECT_EQ(issues_in("prism_recipe = 3\n[hdr]\nheadroom = two\n"), word);
    const std::vector<prism::RecipeIssue> inf = {
        {3, "'inf' is not allowed: a recipe holds finite numbers only"}};
    EXPECT_EQ(issues_in("prism_recipe = 3\n[hdr]\nheadroom = inf\n"), inf);
    const std::vector<prism::RecipeIssue> missing = {{2, "[hdr] needs 'headroom'"}};
    EXPECT_EQ(issues_in("prism_recipe = 3\n[hdr]\n"), missing);
    // No switch: [hdr] has no `on`.
    EXPECT_EQ(issues_in("prism_recipe = 3\n[hdr]\nheadroom = 1\non = false\n").size(), 1u);
}

// --- writing [hdr]
// --------------------------------------------------------------------------------

TEST(RecipeHdr, WrittenOnlyAboveZeroAsVersion3) {
    prism::Recipe r;
    r.exposure = prism::RecipeExposure{1.0f};
    r.hdr = prism::RecipeHdr{2.5f};
    EXPECT_EQ(prism::write_recipe(r, bare),
              "prism_recipe = 3\n\n[exposure]\nstops = +1.0\n\n[hdr]\nheadroom = +2.5\n");
    r.hdr = prism::RecipeHdr{4.0f};
    EXPECT_EQ(prism::write_recipe(r, bare),
              "prism_recipe = 3\n\n[exposure]\nstops = +1.0\n\n[hdr]\nheadroom = +4.0\n");
    // 0 (and -0) is SDR: nothing is written, and the recipe keeps version 1 (or 2).
    for (const float zero : {0.0f, -0.0f}) {
        r.hdr = prism::RecipeHdr{zero};
        EXPECT_EQ(prism::write_recipe(r, bare), "prism_recipe = 1\n\n[exposure]\nstops = +1.0\n");
    }
    // A negative or NaN headroom (never read from a file) writes nothing either: only above 0.
    for (const float bad : {-1.0f, std::nanf("")}) {
        r.hdr = prism::RecipeHdr{bad};
        EXPECT_EQ(prism::write_recipe(r, bare), "prism_recipe = 1\n\n[exposure]\nstops = +1.0\n");
    }
    r.white_balance = prism::RecipeWhiteBalance{5000.0f, 0.0f};
    EXPECT_EQ(prism::write_recipe(r, bare).substr(0, 17), "prism_recipe = 2\n");
    r.hdr = prism::RecipeHdr{0.1f};
    EXPECT_EQ(prism::write_recipe(r, bare).substr(0, 17), "prism_recipe = 3\n");
}

TEST(RecipeHdr, WithCommentsTheLineSaysHowManyTimesWhitesLightItKeeps) {
    // The headroom is in stops, the comment in multiples of white's light, as [exposure]'s is.
    prism::Recipe r;
    r.hdr = prism::RecipeHdr{2.0f};
    std::string text = prism::write_recipe(r);
    EXPECT_NE(text.find("prism_recipe = 3  # recipe format version\n"), std::string::npos) << text;
    EXPECT_NE(text.find("\n[hdr]\nheadroom = +2.0  # the HDR picture keeps light up to 4.00x white's light\n"),
              std::string::npos)
        << text;
    r.hdr = prism::RecipeHdr{1.0f};
    EXPECT_NE(prism::write_recipe(r).find("headroom = +1.0  # the HDR picture keeps light up to 2.00x white's light\n"),
              std::string::npos);
    r.hdr = prism::RecipeHdr{4.0f};
    EXPECT_NE(prism::write_recipe(r).find("headroom = +4.0  # the HDR picture keeps light up to 16.00x white's light\n"),
              std::string::npos);
    // Between the powers of two it is 2^stops, not a rounded step.
    r.hdr = prism::RecipeHdr{2.5f};
    EXPECT_NE(prism::write_recipe(r).find("headroom = +2.5  # the HDR picture keeps light up to 5.66x white's light\n"),
              std::string::npos);
    r.hdr = prism::RecipeHdr{0.1f};
    EXPECT_NE(prism::write_recipe(r).find("headroom = +0.1  # the HDR picture keeps light up to 1.07x white's light\n"),
              std::string::npos);
    // Without comments the line is bare, and the comment never changes what reads back.
    EXPECT_NE(prism::write_recipe(r, bare).find("\n[hdr]\nheadroom = +0.1\n"), std::string::npos);
    EXPECT_EQ(prism::parse_recipe(prism::write_recipe(r)).hdr->headroom, 0.1f);
}

TEST(RecipeHdr, RoundTripsBitForBit) {
    for (const float h : {0.1f, 0.3f, 1.0f, 1.7f, 2.0f, 2.25f, 3.9f, 4.0f, 0.1f + 1e-7f}) {
        prism::Recipe r;
        r.hdr = prism::RecipeHdr{h};
        const prism::Recipe back = prism::parse_recipe(prism::write_recipe(r));
        ASSERT_TRUE(back.hdr) << h;
        EXPECT_EQ(std::bit_cast<std::uint32_t>(back.hdr->headroom), std::bit_cast<std::uint32_t>(h))
            << h;
        EXPECT_EQ(back, r);
    }
}

// --- running it ----------------------------------------------------------------------------------

TEST(RecipeHdr, GoesToProcessSettings) {
    prism::Recipe r;
    EXPECT_FALSE(prism::to_process_settings(r).headroom_stops);
    r.hdr = prism::RecipeHdr{2.0f};
    EXPECT_EQ(prism::to_process_settings(r).headroom_stops, 2.0f);
    r.hdr = prism::RecipeHdr{0.0f};
    EXPECT_EQ(prism::to_process_settings(r).headroom_stops, 0.0f);
    r.hdr = prism::RecipeHdr{4.5f};
    EXPECT_THROW((void)prism::to_process_settings(r), std::invalid_argument);
    r.hdr = prism::RecipeHdr{std::nanf("")};
    EXPECT_THROW((void)prism::to_process_settings(r), std::invalid_argument);
}

TEST(RecipeHdr, TheFlagReplacesTheRecipesHeadroom) {
    prism::Recipe r;
    r.hdr = prism::RecipeHdr{2.0f};
    prism::ProcessSettings flags;
    EXPECT_EQ(prism::merge_recipe_and_flags(r, flags).headroom_stops, 2.0f);
    flags.headroom_stops = 0.0f;  // --headroom 0: SDR only, whatever the recipe says
    EXPECT_EQ(prism::merge_recipe_and_flags(r, flags).headroom_stops, 0.0f);
    flags.headroom_stops = 3.5f;
    EXPECT_EQ(prism::merge_recipe_and_flags(r, flags).headroom_stops, 3.5f);
    EXPECT_EQ(prism::merge_recipe_and_flags(prism::Recipe{}, flags).headroom_stops, 3.5f);
}

// --- files: Windows rules ------------------------------------------------------------------------

TEST(RecipeHdr, WindowsLineEndsAndAByteOrderMarkRead) {
    const std::string crlf = "\xEF\xBB\xBFprism_recipe = 3\r\n\r\n[hdr]\r\nheadroom = +1.5\r\n";
    const prism::Recipe r = prism::parse_recipe(crlf);
    ASSERT_TRUE(r.hdr);
    EXPECT_EQ(r.hdr->headroom, 1.5f);  // the \r is not part of the number
}

TEST(RecipeHdr, SavesAndLoadsThroughAFileInBinary) {
    // A non-ASCII name (é, and a CJK character) as UTF-8 text, as the app and CLI pass names.
    const fs::path path = temp_path("h\xC3\xA9_\xE5\x85\x89.prism");
    prism::Recipe r;
    r.curve = prism::RecipeCurve{prism::CurveShape::filmic};
    r.hdr = prism::RecipeHdr{2.0f};
    prism::save_recipe(path, r);
    const std::string bytes = read_bytes(path);
    EXPECT_EQ(bytes.find('\r'), std::string::npos);  // LF only, on every system
    EXPECT_NE(bytes.find("\n[hdr]\nheadroom = +2.0  # the HDR picture keeps light up to 4.00x white's light\n"),
              std::string::npos);
    EXPECT_EQ(prism::load_recipe(path), r);
    // The same file found by listing its folder is the same path.
    bool found = false;
    for (const fs::directory_entry& e : fs::directory_iterator(path.parent_path())) {
        found = found || e.path() == path;
    }
    EXPECT_TRUE(found);
    fs::remove(path);
}
