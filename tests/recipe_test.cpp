#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "prism/recipe.hpp"

// The .prism recipe format (prism/recipe.hpp, docs/RECIPES.md).

namespace fs = std::filesystem;

namespace {

using prism::CurveShape;
using prism::Recipe;

std::string read_text(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

// The recipe tests/data/recipe_complete.prism holds.
Recipe complete_recipe() {
    Recipe r;
    r.name = "Golden \"test\" recipe";
    r.description = "Every stage, one of them off";
    r.exposure = prism::RecipeExposure{-1.25f, true};
    r.matrix = prism::RecipeMatrix{
        {1.05f, 0.03f, -0.01f, 0.01f, 1.0f, 0.0f, -0.02f, -0.02f, 0.94f}, "grade.txt", true};
    r.saturation = prism::RecipeSaturation{1.2f, false};
    r.contrast = prism::RecipeContrast{0.85f, true};
    r.curve = prism::RecipeCurve{CurveShape::gamma, 2.4f, prism::reinhard_white_default, true};
    return r;
}

// The text write_recipe gives for a recipe with only exposure at `stops`.
std::string stops_line(float stops) {
    Recipe r;
    r.exposure = prism::RecipeExposure{stops, true};
    const std::string text = prism::write_recipe(r);
    const std::size_t start = text.find("stops = ");
    return text.substr(start + 8, text.find('\n', start) - start - 8);
}

// The text written for saturation `amount` (no sign).
std::string amount_line(float amount) {
    Recipe r;
    r.saturation = prism::RecipeSaturation{amount, true};
    const std::string text = prism::write_recipe(r);
    const std::size_t start = text.find("amount = ");
    return text.substr(start + 9, text.find('\n', start) - start - 9);
}

// Floats compared by their bits, so that the round trip is checked to be exact (== would let a
// value through that is off by one in the last place only if both were NaN; here it would also
// let -0 for +0 through, which the writer turns into +0 on purpose: `expected` is normalised).
std::uint32_t bits(float v) {
    return std::bit_cast<std::uint32_t>(v == 0.0f ? 0.0f : v);
}

void expect_same_bits(const Recipe& expected, const Recipe& actual) {
    EXPECT_EQ(expected.name, actual.name);
    EXPECT_EQ(expected.description, actual.description);
    ASSERT_EQ(expected.exposure.has_value(), actual.exposure.has_value());
    if (expected.exposure) {
        EXPECT_EQ(bits(expected.exposure->stops), bits(actual.exposure->stops));
        EXPECT_EQ(expected.exposure->on, actual.exposure->on);
    }
    ASSERT_EQ(expected.matrix.has_value(), actual.matrix.has_value());
    if (expected.matrix) {
        for (std::size_t i = 0; i < 9; ++i) {
            EXPECT_EQ(bits(expected.matrix->values[i]), bits(actual.matrix->values[i])) << i;
        }
        EXPECT_EQ(expected.matrix->from, actual.matrix->from);
        EXPECT_EQ(expected.matrix->on, actual.matrix->on);
    }
    ASSERT_EQ(expected.saturation.has_value(), actual.saturation.has_value());
    if (expected.saturation) {
        EXPECT_EQ(bits(expected.saturation->amount), bits(actual.saturation->amount));
        EXPECT_EQ(expected.saturation->on, actual.saturation->on);
    }
    ASSERT_EQ(expected.contrast.has_value(), actual.contrast.has_value());
    if (expected.contrast) {
        EXPECT_EQ(bits(expected.contrast->amount), bits(actual.contrast->amount));
        EXPECT_EQ(expected.contrast->on, actual.contrast->on);
    }
    ASSERT_EQ(expected.curve.has_value(), actual.curve.has_value());
    if (expected.curve) {
        EXPECT_EQ(expected.curve->shape, actual.curve->shape);
        EXPECT_EQ(bits(expected.curve->gamma), bits(actual.curve->gamma));
        EXPECT_EQ(bits(expected.curve->white), bits(actual.curve->white));
        EXPECT_EQ(expected.curve->on, actual.curve->on);
    }
}

// The issues parse_recipe reports for `text`, or none if it reads.
std::vector<prism::RecipeIssue> issues_of(const std::string& text) {
    try {
        (void)prism::parse_recipe(text, "test.prism");
    } catch (const prism::RecipeError& e) {
        return e.issues();
    }
    return {};
}

// Random recipes for the round trip: every float a valid value can be, whatever its size, and
// names made of any characters, including quotes, backslashes and control characters.
class RandomRecipes {
public:
    explicit RandomRecipes(std::uint32_t seed) : rng_(seed) {}

    Recipe next() {
        Recipe r;
        if (coin()) {
            r.name = text();
        }
        if (coin()) {
            r.description = text();
        }
        if (coin()) {
            r.exposure = prism::RecipeExposure{finite(), coin()};
        }
        if (coin()) {
            prism::RecipeMatrix m;
            for (float& v : m.values) {
                v = finite();
            }
            if (coin()) {
                m.from = "grade_" + std::to_string(rng_() % 1000) + ".txt";
            }
            m.on = coin();
            r.matrix = m;
        }
        if (coin()) {
            r.saturation = prism::RecipeSaturation{in(prism::saturation_range), coin()};
        }
        if (coin()) {
            r.contrast = prism::RecipeContrast{in(prism::contrast_range), coin()};
        }
        if (coin()) {
            prism::RecipeCurve c;
            c.shape = static_cast<CurveShape>(rng_() % 4);
            if (c.shape == CurveShape::gamma) {
                c.gamma = positive();
            } else if (c.shape == CurveShape::reinhard) {
                c.white = positive();
            }
            c.on = coin();
            r.curve = c;
        }
        return r;
    }

private:
    bool coin() { return (rng_() & 1) != 0; }

    // Any finite float, from a random bit pattern half the time (tiny, huge, -0, subnormal...)
    // and a value of everyday size the other half.
    float finite() {
        if (coin()) {
            float v = 0;
            do {
                v = std::bit_cast<float>(static_cast<std::uint32_t>(rng_()));
            } while (!std::isfinite(v));
            return v;
        }
        return std::uniform_real_distribution<float>(-4.0f, 4.0f)(rng_);
    }

    float positive() {
        float v = 0;
        do {
            v = std::fabs(finite());
        } while (!(v > 0.0f));
        return v;
    }

    float in(prism::ValueRange range) {
        // A random bit pattern between the limits' own, so every float in the range can come up.
        const std::uint32_t lo = std::bit_cast<std::uint32_t>(static_cast<float>(range.min));
        const std::uint32_t hi = std::bit_cast<std::uint32_t>(static_cast<float>(range.max));
        return std::bit_cast<float>(std::uniform_int_distribution<std::uint32_t>(lo, hi)(rng_));
    }

    std::string text() {
        std::string out;
        const int length = static_cast<int>(rng_() % 12);
        for (int i = 0; i < length; ++i) {
            std::uint32_t cp = 0;
            switch (rng_() % 4) {
                case 0: cp = rng_() % 0x80; break;  // ASCII, control characters included
                case 1: cp = static_cast<unsigned char>("\"\\#=[]"[rng_() % 6]); break;
                case 2: cp = 0x80 + rng_() % 0x780; break;
                default: do { cp = 0x800 + rng_() % (0x110000 - 0x800);
                    } while (cp >= 0xD800 && cp <= 0xDFFF);
            }
            append(out, cp);
        }
        return out;
    }

    static void append(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::mt19937 rng_;
};

}  // namespace

// --- Writing -------------------------------------------------------------------------------------

TEST(RecipeWrite, MatchesTheGoldenFileByteForByte) {
    const std::string golden = read_text(fs::path(PRISM_TEST_DATA) / "recipe_complete.prism");
    ASSERT_FALSE(golden.empty());
    EXPECT_EQ(prism::write_recipe(complete_recipe()), golden);
}

TEST(RecipeWrite, AnEmptyRecipeIsJustTheVersion) {
    EXPECT_EQ(prism::write_recipe(Recipe{}), "prism_recipe = 1\n");
}

TEST(RecipeWrite, NumbersAreShortestAndWholeNumbersGetADecimal) {
    EXPECT_EQ(amount_line(1.0f), "1.0");
    EXPECT_EQ(amount_line(10.0f), "10.0");
    EXPECT_EQ(amount_line(0.1f), "0.1");                // not 0.100000001
    EXPECT_EQ(amount_line(1.0f / 3.0f), "0.33333334");  // as many digits as it takes
    EXPECT_EQ(amount_line(std::nextafter(1.0f, 2.0f)), "1.0000001");
    EXPECT_EQ(amount_line(1e-7f), "1e-07");  // a valid TOML float as it is
    EXPECT_EQ(amount_line(3e+20f), "3e+20");
}

TEST(RecipeWrite, ExposureAlwaysHasASignAndMinusZeroIsZero) {
    EXPECT_EQ(stops_line(1.0f), "+1.0");
    EXPECT_EQ(stops_line(0.7f), "+0.7");
    EXPECT_EQ(stops_line(-2.0f), "-2.0");
    EXPECT_EQ(stops_line(0.0f), "+0.0");
    EXPECT_EQ(stops_line(-0.0f), "+0.0");
    EXPECT_EQ(amount_line(-0.0f), "0.0");
}

TEST(RecipeWrite, SectionsComeInPipelineOrderWithoutAlignment) {
    const std::string text = prism::write_recipe(complete_recipe());
    std::size_t last = 0;
    for (const char* section :
         {"[exposure]", "[matrix]", "[saturation]", "[contrast]", "[curve]"}) {
        const std::size_t at = text.find(section);
        ASSERT_NE(at, std::string::npos) << section;
        EXPECT_GT(at, last) << section;
        last = at;
    }
    EXPECT_EQ(text.find("  ="), std::string::npos) << "no aligned = signs";
    EXPECT_EQ(text.find("=  "), std::string::npos);
    EXPECT_EQ(text.find('\r'), std::string::npos);
    EXPECT_NE(text.substr(0, 3), "\xEF\xBB\xBF") << "no byte-order mark";
}

TEST(RecipeWrite, OnlyTheChosenCurvesParameterIsWritten) {
    Recipe r;
    r.curve = prism::RecipeCurve{CurveShape::reinhard, 3.0f, 6.0f, true};
    EXPECT_EQ(prism::write_recipe(r),
              "prism_recipe = 1\n\n[curve]\nshape = \"reinhard\"\nwhite = 6.0\n");
    r.curve->shape = CurveShape::filmic;
    r.curve->on = false;
    EXPECT_EQ(prism::write_recipe(r),
              "prism_recipe = 1\n\n[curve]\nshape = \"filmic\"\non = false\n");
}

TEST(RecipeWrite, TextIsEscapedAndStaysOnOneLine) {
    Recipe r;
    r.name = std::string("a \"b\" \\ c\td\ne\x01 f\x7F café 写真");
    EXPECT_EQ(
        prism::write_recipe(r),
        "prism_recipe = 1\nname = \"a \\\"b\\\" \\\\ c\\td\\ne\\u0001 f\\u007F café 写真\"\n");
}

// --- Reading valid recipes -----------------------------------------------------------------------

TEST(RecipeRead, TheSmallestRecipeIsJustTheVersion) {
    EXPECT_EQ(prism::parse_recipe("prism_recipe = 1\n"), Recipe{});
    EXPECT_EQ(prism::parse_recipe("prism_recipe = 1"), Recipe{}) << "no newline at the end";
}

TEST(RecipeRead, TheGoldenFileReadsBack) {
    const std::string golden = read_text(fs::path(PRISM_TEST_DATA) / "recipe_complete.prism");
    expect_same_bits(complete_recipe(), prism::parse_recipe(golden));
}

TEST(RecipeRead, CommentsBlankLinesAndSpacesAreIgnored) {
    const Recipe r = prism::parse_recipe(
        "# A comment first\n"
        "\n"
        "   prism_recipe=1   # the version\n"
        "\t\n"
        "name = \"has # inside\" # not part of the name\n"
        "  [ exposure ]   # a section\n"
        "\tstops\t=\t-0.5\t\n"
        "   # an indented comment\n"
        "on = false#no space before the comment\n");
    EXPECT_EQ(r.name, "has # inside");
    ASSERT_TRUE(r.exposure);
    EXPECT_EQ(r.exposure->stops, -0.5f);
    EXPECT_FALSE(r.exposure->on);
}

TEST(RecipeRead, WindowsLineEndsAreAccepted) {
    // Built here: .gitattributes would turn a CRLF file in the repository into LF.
    const std::string lf = read_text(fs::path(PRISM_TEST_DATA) / "recipe_complete.prism");
    std::string crlf;
    for (const char c : lf) {
        crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
    }
    ASSERT_NE(crlf, lf);
    expect_same_bits(complete_recipe(), prism::parse_recipe(crlf));
}

TEST(RecipeRead, AByteOrderMarkIsAccepted) {
    const Recipe r =
        prism::parse_recipe("\xEF\xBB\xBFprism_recipe = 1\n[saturation]\namount = 2\n");
    ASSERT_TRUE(r.saturation);
    EXPECT_EQ(r.saturation->amount, 2.0f);
}

TEST(RecipeRead, SectionsMayComeInAnyOrder) {
    const Recipe r = prism::parse_recipe(
        "prism_recipe = 1\n"
        "[curve]\nshape = \"filmic\"\n"
        "[contrast]\namount = 1.1\n"
        "[exposure]\nstops = +1.0\n");
    ASSERT_TRUE(r.curve && r.contrast && r.exposure);
    EXPECT_FALSE(r.matrix || r.saturation);
    EXPECT_EQ(r.curve->shape, CurveShape::filmic);
    EXPECT_EQ(r.contrast->amount, 1.1f);
    EXPECT_EQ(r.exposure->stops, 1.0f);
}

TEST(RecipeRead, WholeNumbersAreFineForDecimalKeys) {
    const Recipe r = prism::parse_recipe(
        "prism_recipe = 1\n"
        "[exposure]\nstops = -2\n"
        "[matrix]\nred = [1, 0, 0]\ngreen = [0, 1, 0]\n"
        "blue = [0, 0, 1,]   # a comma after the last is fine\n"
        "[saturation]\namount = 0\n"
        "[contrast]\namount = +3\n"
        "[curve]\nshape = \"reinhard\"\nwhite = 1_000\n");
    EXPECT_EQ(r.exposure->stops, -2.0f);
    EXPECT_EQ(r.matrix->values, prism::identity_matrix);
    EXPECT_EQ(r.saturation->amount, 0.0f);
    EXPECT_EQ(r.contrast->amount, 3.0f);
    EXPECT_EQ(r.curve->white, 1000.0f);
}

TEST(RecipeRead, NumbersReadStraightToTheNearestFloat) {
    // 1.00000005960464477539 lies exactly half-way between two floats (1 and 1 + 2^-23); just
    // above it must round up. Reading it as a double first and then rounding to float would get
    // the first one right but could get values like it wrong; strtof rounds once.
    const Recipe r = prism::parse_recipe(
        "prism_recipe = 1\n[exposure]\nstops = 1.000000059604644775390625000001\n");
    EXPECT_EQ(r.exposure->stops, std::nextafter(1.0f, 2.0f));
    EXPECT_EQ(prism::parse_recipe("prism_recipe = 1\n[exposure]\nstops = 1e-45\n").exposure->stops,
              std::numeric_limits<float>::denorm_min());
    EXPECT_EQ(prism::parse_recipe("prism_recipe = 1\n[exposure]\nstops = 2.5E+1\n").exposure->stops,
              25.0f);
}

TEST(RecipeRead, MissingSecondaryKeysTakeTheirDefaults) {
    const Recipe r = prism::parse_recipe("prism_recipe = 1\n[curve]\nshape = \"gamma\"\n");
    ASSERT_TRUE(r.curve);
    EXPECT_EQ(r.curve->shape, CurveShape::gamma);
    EXPECT_EQ(r.curve->gamma, prism::gamma_default);
    EXPECT_EQ(r.curve->white, prism::reinhard_white_default);
    EXPECT_TRUE(r.curve->on);
}

TEST(RecipeRead, TextEscapesAreDecoded) {
    const Recipe r = prism::parse_recipe(
        "prism_recipe = 1\nname = \"caf\\u00E9 \\U0001F4F7 \\\"q\\\" \\\\ \\t|\"\n"
        "description = \"Plain UTF-8: café 写真\"\n");
    EXPECT_EQ(r.name, "café \xF0\x9F\x93\xB7 \"q\" \\ \t|");
    EXPECT_EQ(r.description, "Plain UTF-8: café 写真");
}

// --- Round trips ---------------------------------------------------------------------------------

TEST(RecipeRoundTrip, TheCompleteRecipeReadsBackBitForBit) {
    expect_same_bits(complete_recipe(),
                     prism::parse_recipe(prism::write_recipe(complete_recipe())));
}

TEST(RecipeRoundTrip, MinusZeroComesBackAsZero) {
    Recipe r;
    r.exposure = prism::RecipeExposure{-0.0f, true};
    const Recipe back = prism::parse_recipe(prism::write_recipe(r));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(back.exposure->stops), 0u);
}

TEST(RecipeRoundTrip, TenThousandRandomRecipes) {
    RandomRecipes random(20261103);
    for (int i = 0; i < 10000; ++i) {
        const Recipe r = random.next();
        const std::string text = prism::write_recipe(r);
        Recipe back;
        try {
            back = prism::parse_recipe(text);
        } catch (const prism::RecipeError& e) {
            FAIL() << "recipe " << i << " does not read back:\n" << e.what() << "\n" << text;
        }
        expect_same_bits(r, back);
        ASSERT_EQ(prism::write_recipe(back), text) << "recipe " << i << " is not a fixed point";
        if (::testing::Test::HasFailure()) {
            FAIL() << "recipe " << i << ":\n" << text;
        }
    }
}

// --- Errors --------------------------------------------------------------------------------------

namespace {

struct ErrorCase {
    const char* name;
    std::string text;
    int line;
    std::string message;
};

std::string with_section(const std::string& section, const std::string& body) {
    return "prism_recipe = 1\n\n[" + section + "]\n" + body;  // body starts on line 4
}

const ErrorCase error_cases[] = {
    {"UnknownKey", with_section("saturation", "amout = 0.9\n"), 4,
     "unknown setting 'amout' in [saturation]. Did you mean 'amount'?"},
    {"UnknownKeyFarFromAny", with_section("exposure", "brightness = 1\n"), 4,
     "unknown setting 'brightness' in [exposure]"},
    {"UnknownTopKey", "prism_recipe = 1\nnmae = \"Warm\"\n", 2,
     "unknown setting 'nmae'. Did you mean 'name'?"},
    {"UnknownSection", "prism_recipe = 1\n[contrst]\namount = 1\n", 2,
     "unknown section [contrst]. Did you mean [contrast]?"},
    {"UnknownSectionFarFromAny", "prism_recipe = 1\n[vignette]\n", 2, "unknown section [vignette]"},
    {"DuplicateKey", with_section("contrast", "amount = 1\namount = 2\n"), 5,
     "'amount' is set twice in [contrast] (first on line 4)"},
    {"DuplicateTopKey", "prism_recipe = 1\nname = \"a\"\nname = \"b\"\n", 3,
     "'name' is set twice (first on line 2)"},
    {"DuplicateSection", with_section("exposure", "stops = 1\n[exposure]\nstops = 2\n"), 5,
     "[exposure] appears twice (first on line 3)"},
    {"KeyOutsideASection", "prism_recipe = 1\nstops = +1.0\n", 2,
     "'stops' belongs in a section: put it under [exposure]"},
    {"SharedKeyOutsideASection", "prism_recipe = 1\namount = 1\n", 2,
     "'amount' belongs in a section: put it under [saturation] or [contrast]"},
    {"SwitchOutsideASection", "prism_recipe = 1\non = false\n", 2,
     "'on' belongs in the section of the stage it switches, such as [exposure]"},
    {"TopKeyInASection", with_section("exposure", "stops = 1\nname = \"Warm\"\n"), 5,
     "'name' belongs at the top of the file, before the first [section]"},
    {"SaturationAboveRange", with_section("saturation", "amount = 12\n"), 4,
     "[saturation] amount must be from 0 to 10 (it is 12)"},
    {"SaturationBelowRange", with_section("saturation", "amount = -0.5\n"), 4,
     "[saturation] amount must be from 0 to 10 (it is -0.5)"},
    {"ContrastBelowRange", with_section("contrast", "amount = 0.05\n"), 4,
     "[contrast] amount must be from 0.1 to 10 (it is 0.05)"},
    {"GammaZero", with_section("curve", "shape = \"gamma\"\ngamma = 0\n"), 5,
     "[curve] gamma must be a number above 0 (it is 0)"},
    {"WhiteNegative", with_section("curve", "shape = \"reinhard\"\nwhite = -4\n"), 5,
     "[curve] white must be a number above 0 (it is -4)"},
    {"UnitAfterASpace", with_section("exposure", "stops = +1 stop\n"), 4,
     "'+1 stop' is not a number: write the number alone, without a unit"},
    {"UnitAttached", with_section("contrast", "amount = 1.2x\n"), 4,
     "'1.2x' is not a number: write the number alone, without a unit"},
    {"Percent", with_section("saturation", "amount = 120%\n"), 4,
     "'120%' is not a number: write the number alone, without a unit"},
    {"DecimalComma", with_section("saturation", "amount = 0,9\n"), 4,
     "'0,9' is not a number: use a dot for decimals, as in 0.9"},
    {"DecimalCommasInARow",
     with_section("matrix", "red = [1,05, 0,03, -0,01]\ngreen = [0, 1, 0]\nblue = [0, 0, 1]\n"), 4,
     "'[1,05, 0,03, -0,01]' seems to use commas for decimals: write each number with a dot, and a "
     "comma between numbers, as in [1.05, 0.03, -0.01]"},
    {"Hexadecimal", with_section("exposure", "stops = 0x10\n"), 4,
     "'0x10' is not a number Prism reads: write it in decimal"},
    {"Infinity", with_section("exposure", "stops = inf\n"), 4,
     "'inf' is not allowed: a recipe holds finite numbers only"},
    {"MinusInfinity", with_section("exposure", "stops = -inf\n"), 4,
     "'-inf' is not allowed: a recipe holds finite numbers only"},
    {"NotANumber", with_section("contrast", "amount = nan\n"), 4,
     "'nan' is not allowed: a recipe holds finite numbers only"},
    {"LeadingDot", with_section("saturation", "amount = .5\n"), 4,
     "'.5' needs a digit before the dot, as in 0.5"},
    {"TrailingDot", with_section("saturation", "amount = 5.\n"), 4,
     "'5.' needs a digit after the dot, as in 5.0"},
    {"LeadingZero", with_section("contrast", "amount = 01.5\n"), 4,
     "'01.5' is not a number: write it without leading zeros"},
    {"TooLargeForAFloat", with_section("exposure", "stops = 1e39\n"), 4,
     "'1e39' is too large: a recipe's numbers must be below 3.4e38"},
    {"TooCloseToZero", with_section("exposure", "stops = 1e-50\n"), 4,
     "'1e-50' is too close to 0 for Prism to hold: write 0 instead"},
    {"WordForANumber", with_section("exposure", "stops = lots\n"), 4, "'lots' is not a number"},
    {"QuotedNumber", with_section("exposure", "stops = \"1.0\"\n"), 4,
     "'stops' needs a number, not text in quotes"},
    {"UnquotedText", with_section("curve", "shape = filmic\n"), 4,
     "text needs double quotes, as in shape = \"filmic\""},
    {"UnquotedWords", "prism_recipe = 1\nname = Warm low contrast\n", 2,
     "text needs double quotes, as in name = \"Warm low contrast\""},
    {"SingleQuotes", "prism_recipe = 1\nname = 'Warm'\n", 2,
     "text needs double quotes, not single ones: name = \"...\""},
    {"UnterminatedText", "prism_recipe = 1\nname = \"Warm\n", 2,
     "the text starting with \" is not closed: end it with \" on the same line"},
    {"TripleQuotes", "prism_recipe = 1\ndescription = \"\"\"Warm\"\"\"\n", 2,
     "text in triple quotes is not part of a recipe: write it on one line in double quotes"},
    {"UnknownEscape", "prism_recipe = 1\nname = \"C:\\Photos\"\n", 2,
     "unknown escape '\\P' in text: write \\\\ for a backslash"},
    {"InvalidUtf8", "prism_recipe = 1\n# caf\xC3\n", 2, "the line is not valid UTF-8 text"},
    {"Utf16Surrogate", "prism_recipe = 1\nname = \"\xED\xA0\x80\"\n", 2,
     "the line is not valid UTF-8 text"},
    {"NulByte", std::string("prism_recipe = 1\nname = \"a\0b\"\n", 30), 2,
     "the line has a NUL byte: this does not look like a text file"},
    {"ControlCharacter", "prism_recipe = 1\n# a bell \x07 here\n", 2,
     "the line has a control character (byte 0x07): write it in text as an escape such as \\t or "
     "\\n"},
    {"StrayCarriageReturn", "prism_recipe = 1\r\r\n", 1,
     "the line has a control character (byte 0x0D): write it in text as an escape such as \\t or "
     "\\n"},
    {"LargerThan64KB", "prism_recipe = 1\n" + std::string(64 * 1024, '#'), 0,
     "the file is larger than 64 KB, too large to be a recipe"},
    {"Empty", "", 0, "there are no settings: a recipe starts with prism_recipe = 1"},
    {"OnlyComments", "# nothing here\n\n", 0,
     "there are no settings: a recipe starts with prism_recipe = 1"},
    {"SectionBeforeTheVersion", "[exposure]\nstops = 1\n", 1,
     "a recipe starts with prism_recipe = 1, before any [section]"},
    {"SettingBeforeTheVersion", "name = \"Warm\"\nprism_recipe = 1\n", 1,
     "a recipe starts with prism_recipe = 1, before any other setting"},
    {"VersionNotWhole", "prism_recipe = 1.0\n", 1,
     "prism_recipe must be a whole number, the format version, as in prism_recipe = 1 (it is 1.0)"},
    {"VersionZero", "prism_recipe = 0\n", 1, "prism_recipe must be 1 or more (it is 0)"},
    {"MissingMainKey", with_section("exposure", "on = false\n"), 3, "[exposure] needs 'stops'"},
    {"MissingMatrixRow", with_section("matrix", "red = [1, 0, 0]\ngreen = [0, 1, 0]\n"), 3,
     "[matrix] needs 'blue'"},
    {"MissingShape", with_section("curve", "gamma = 2.2\n"), 3, "[curve] needs 'shape'"},
    {"RowWithTwoNumbers",
     with_section("matrix", "red = [1.0, 0.0]\ngreen = [0, 1, 0]\nblue = [0, 0, 1]\n"), 4,
     "'red' needs 3 numbers, as in [1.0, 0.0, 0.0] (it has 2)"},
    {"RowNotAList", with_section("matrix", "red = 1.0\ngreen = [0, 1, 0]\nblue = [0, 0, 1]\n"), 4,
     "'red' needs 3 numbers in square brackets, as in [1.0, 0.0, 0.0]"},
    {"RowWithText",
     with_section("matrix", "red = [1, \"0\", 0]\ngreen = [0, 1, 0]\nblue = [0, 0, 1]\n"), 4,
     "the list must hold numbers only"},
    {"RowNotClosed", with_section("matrix", "red = [1.0, 0.0, 0.0\n"), 4,
     "the list is not closed: end it with ] on the same line"},
    {"RowWithAnEmptyItem",
     with_section("matrix", "red = [1.0, , 0.0]\ngreen = [0, 1, 0]\nblue = [0, 0, 1]\n"), 4,
     "the list has an empty item: remove the extra comma"},
    {"SwitchAsANumber", with_section("exposure", "stops = 1\non = 1\n"), 5,
     "'on' must be true or false (it is 1)"},
    {"SwitchCapitalised", with_section("exposure", "stops = 1\non = True\n"), 5,
     "'on' must be true or false (it is True)"},
    {"UnknownShape", with_section("curve", "shape = \"film\"\n"), 4,
     "unknown curve shape \"film\". Did you mean \"filmic\"?"},
    {"UnknownShapeFarFromAny", with_section("curve", "shape = \"sigmoid\"\n"), 4,
     "unknown curve shape \"sigmoid\": use \"none\", \"gamma\", \"reinhard\" or \"filmic\""},
    {"ParameterOfAnotherShape", with_section("curve", "shape = \"filmic\"\ngamma = 2.2\n"), 5,
     "'gamma' only applies to shape = \"gamma\" (this curve is \"filmic\")"},
    {"MatrixFromWithAFolder",
     with_section("matrix",
                  "red = [1, 0, 0]\ngreen = [0, 1, 0]\nblue = [0, 0, 1]\n"
                  "from = \"grades/warm.txt\"\n"),
     7, "'from' must be a file name only, without folders (it is \"grades/warm.txt\")"},
    {"SomethingAfterTheValue", with_section("exposure", "stops = 1.0 2.0\n"), 4,
     "unexpected '2.0' after the value of 'stops'"},
    {"SomethingAfterText", "prism_recipe = 1\nname = \"Warm\" low\n", 2,
     "unexpected 'low' after the value of 'name'"},
    {"NoEqualsSign", with_section("exposure", "stops 1.0\n"), 4,
     "'stops 1.0' is not a setting: write it as key = value"},
    {"NoValue", with_section("exposure", "stops =\n"), 4, "'stops' has no value"},
    {"DottedKey", "prism_recipe = 1\nexposure.stops = 1\n", 2,
     "write the setting's name without dots: put it under its [section]"},
    {"QuotedKey", with_section("exposure", "\"stops\" = 1\n"), 4,
     "write the setting's name without quotes, as in stops = +1.0"},
    {"ListOfSections", "prism_recipe = 1\n[[exposure]]\n", 2,
     "lists of sections ([[...]]) are not part of a recipe"},
    {"UnclosedSection", "prism_recipe = 1\n[exposure\n", 2,
     "the section name is missing its closing ]"},
    {"DottedSection", "prism_recipe = 1\n[curve.gamma]\n", 2,
     "[curve.gamma] is not a section name: write it without quotes, dots or spaces, as in "
     "[exposure]"},
    {"NewerFormat", "prism_recipe = 2\n\n[exposure]\nstops = +1.0\n", 1,
     "this recipe needs recipe format 2, but this Prism reads up to format 1: update Prism to "
     "read it"},
};

class RecipeErrors : public ::testing::TestWithParam<ErrorCase> {};

}  // namespace

TEST_P(RecipeErrors, GiveTheLineAndThePlainWordsMessage) {
    const ErrorCase& c = GetParam();
    const std::vector<prism::RecipeIssue> issues = issues_of(c.text);
    ASSERT_EQ(issues.size(), 1u) << ::testing::PrintToString(issues.size()) << " issues for:\n"
                                 << c.text
                                 << (issues.empty() ? "" : "\nfirst: " + issues[0].message);
    EXPECT_EQ(issues[0].line, c.line);
    EXPECT_EQ(issues[0].message, c.message);
}

INSTANTIATE_TEST_SUITE_P(Table, RecipeErrors, ::testing::ValuesIn(error_cases),
                         [](const ::testing::TestParamInfo<ErrorCase>& info) {
                             return std::string(info.param.name);
                         });

TEST(RecipeErrorList, SeveralMistakesAreReportedTogetherInLineOrder) {
    const std::vector<prism::RecipeIssue> issues = issues_of(
        "prism_recipe = 1\n"  // 1
        "[saturation]\n"      // 2
        "amout = 0.9\n"       // 3
        "[contrast]\n"        // 4
        "amount = 15\n"       // 5
        "[curve]\n"           // 6
        "shape = filmic\n");  // 7
    const std::vector<prism::RecipeIssue> expected = {
        {3, "unknown setting 'amout' in [saturation]. Did you mean 'amount'?"},
        {5, "[contrast] amount must be from 0.1 to 10 (it is 15)"},
        {7, "text needs double quotes, as in shape = \"filmic\""},
    };
    EXPECT_EQ(issues, expected);
}

TEST(RecipeErrorList, MissingKeysAreFoundAtTheEndButReportedInLineOrder) {
    const std::vector<prism::RecipeIssue> issues = issues_of("prism_recipe = 1\n"
                                                             "[exposure]\n"
                                                             "on = false\n"
                                                             "[contrast]\n"
                                                             "amount = 15\n");
    const std::vector<prism::RecipeIssue> expected = {
        {2, "[exposure] needs 'stops'"},
        {5, "[contrast] amount must be from 0.1 to 10 (it is 15)"},
    };
    EXPECT_EQ(issues, expected);
}

TEST(RecipeErrorList, StopsAtTenMistakes) {
    std::string text = "prism_recipe = 1\n[exposure]\nstops = 0\n";
    for (int i = 0; i < 25; ++i) {
        text += "bad" + std::to_string(i) + " = 1\n";
    }
    const std::vector<prism::RecipeIssue> issues = issues_of(text);
    ASSERT_EQ(issues.size(), prism::recipe_max_errors);
    EXPECT_EQ(issues.back().line, 13);
}

TEST(RecipeErrorList, MessagesNameTheSourceAndTheLine) {
    try {
        (void)prism::parse_recipe("prism_recipe = 1\n[contrast]\namount = 15\non = maybe\n",
                                  "warm.prism");
        FAIL() << "should not read";
    } catch (const prism::RecipeError& e) {
        EXPECT_EQ(e.source(), "warm.prism");
        EXPECT_STREQ(e.what(),
                     "recipe 'warm.prism' line 3: [contrast] amount must be from 0.1 to 10 (it is "
                     "15)\n"
                     "recipe 'warm.prism' line 4: 'on' must be true or false (it is maybe)");
    }
    EXPECT_EQ(prism::format_recipe_issue("", {0, "the file is larger than 64 KB"}),
              "recipe: the file is larger than 64 KB");
    EXPECT_EQ(prism::format_recipe_issue("a.prism", {0, "x"}), "recipe 'a.prism': x");
}
