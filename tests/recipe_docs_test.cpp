#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "prism/recipe.hpp"

// Keeps docs/RECIPES.md in step with the parser:
//   * the table under "## Every setting" lists exactly the keys of prism::recipe_format(), with
//     the unit, values, default and version the table in lib/recipe.cpp gives them;
//   * every example marked <!-- recipe: valid --> reads;
//   * the example marked <!-- recipe: saved --> is exactly what write_recipe writes for it;
//   * every example marked <!-- recipe: error line N: message --> gives that line and message.
// A marker applies to the ```toml block right after it.

namespace {

std::string read_text(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

const std::string& doc() {
    static const std::string text = read_text(PRISM_RECIPE_DOC);
    return text;
}

struct Example {
    std::string marker;  // what follows "<!-- recipe: " up to " -->"
    std::string text;    // the code block's contents
};

std::vector<Example> examples() {
    std::vector<Example> out;
    const std::string& d = doc();
    const std::string open = "<!-- recipe: ";
    for (std::size_t at = d.find(open); at != std::string::npos; at = d.find(open, at + 1)) {
        const std::size_t end = d.find(" -->", at);
        const std::string marker = d.substr(at + open.size(), end - at - open.size());
        const std::size_t fence = d.find("```toml\n", end);
        const std::size_t close = d.find("\n```", fence);
        // The block must follow the marker directly.
        EXPECT_EQ(d.substr(end + 4, fence - end - 4), "\n") << marker;
        out.push_back({marker, d.substr(fence + 8, close + 1 - fence - 8)});
    }
    return out;
}

std::string strip(std::string cell) {
    std::string out;
    for (const char c : cell) {
        if (c != '`') {
            out += c;
        }
    }
    const std::size_t first = out.find_first_not_of(' ');
    const std::size_t last = out.find_last_not_of(' ');
    return first == std::string::npos ? std::string() : out.substr(first, last - first + 1);
}

// section, key, unit, values, default, since
using Row =
    std::tuple<std::string, std::string, std::string, std::string, std::string, std::string>;

std::set<Row> rows_in_doc() {
    std::set<Row> rows;
    const std::string& d = doc();
    std::size_t at = d.find("\n## Every setting\n");
    EXPECT_NE(at, std::string::npos);
    at = d.find("\n| Section | Key | Unit | Values | Default | Since |\n|---|", at);
    EXPECT_NE(at, std::string::npos);
    at = d.find('\n', d.find("|---", at)) + 1;  // the first row
    std::istringstream lines(d.substr(at));
    for (std::string line; std::getline(lines, line) && line.rfind("| ", 0) == 0;) {
        std::vector<std::string> cells;
        std::size_t start = 1;
        for (std::size_t bar = line.find('|', start); bar != std::string::npos;
             bar = line.find('|', start)) {
            cells.push_back(strip(line.substr(start, bar - start)));
            start = bar + 1;
        }
        EXPECT_EQ(cells.size(), 6u) << line;
        if (cells.size() != 6) {
            continue;
        }
        std::string section = cells[0];
        if (section == "(top)") {
            section.clear();
        } else if (section.size() > 2 && section.front() == '[' && section.back() == ']') {
            section = section.substr(1, section.size() - 2);
        }
        rows.insert({section, cells[1], cells[2], cells[3], cells[4], cells[5]});
    }
    return rows;
}

std::set<Row> rows_in_parser() {
    std::set<Row> rows;
    for (const prism::RecipeKeySpec& spec : prism::recipe_format().keys) {
        rows.insert({std::string(spec.section), std::string(spec.key), std::string(spec.unit),
                     prism::describe_values(spec), prism::describe_default(spec),
                     std::to_string(spec.since)});
    }
    return rows;
}

}  // namespace

TEST(RecipeDocs, TheKeyTableMatchesTheParsersTableBothWays) {
    const std::set<Row> documented = rows_in_doc();
    const std::set<Row> parsed = rows_in_parser();
    EXPECT_EQ(documented.size(), prism::recipe_format().keys.size());
    for (const Row& row : parsed) {
        EXPECT_TRUE(documented.count(row) == 1)
            << "RECIPES.md is missing, or describes differently: [" << std::get<0>(row) << "] "
            << std::get<1>(row) << " | " << std::get<2>(row) << " | " << std::get<3>(row) << " | "
            << std::get<4>(row) << " | " << std::get<5>(row);
    }
    for (const Row& row : documented) {
        EXPECT_TRUE(parsed.count(row) == 1)
            << "RECIPES.md documents what the parser does not have: [" << std::get<0>(row) << "] "
            << std::get<1>(row) << " | " << std::get<2>(row) << " | " << std::get<3>(row) << " | "
            << std::get<4>(row) << " | " << std::get<5>(row);
    }
}

TEST(RecipeDocs, EveryExampleMarkedValidReads) {
    int count = 0;
    for (const Example& e : examples()) {
        if (e.marker == "valid") {
            ++count;
            EXPECT_NO_THROW((void)prism::parse_recipe(e.text, "RECIPES.md example")) << e.text;
        }
    }
    EXPECT_GE(count, 1);
}

TEST(RecipeDocs, TheHandWrittenExampleIsTheWarmGradeFromTheSamples) {
    for (const Example& e : examples()) {
        if (e.marker != "valid") {
            continue;
        }
        const prism::Recipe r = prism::parse_recipe(e.text);
        EXPECT_EQ(r.name, "Warm low contrast");
        // The text says its matrix is samples/grade.txt's, and that it leaves exposure and the
        // tone curve alone.
        ASSERT_TRUE(r.matrix);
        EXPECT_EQ(r.matrix->values, prism::load_matrix(PRISM_GRADE_SAMPLE));
        EXPECT_EQ(r.matrix->from, "grade.txt");
        EXPECT_FALSE(r.exposure);
        EXPECT_FALSE(r.curve);
        return;
    }
    FAIL() << "no example marked valid";
}

TEST(RecipeDocs, TheSavedExampleIsExactlyWhatPrismWrites) {
    int count = 0;
    for (const Example& e : examples()) {
        if (e.marker == "saved") {
            ++count;
            EXPECT_EQ(prism::write_recipe(prism::parse_recipe(e.text)), e.text);
        }
    }
    EXPECT_EQ(count, 1);
}

TEST(RecipeDocs, TheSavedExampleIsTheHandWrittenOneWithExposureAndACurve) {
    // As the text says: the same recipe under a new name, saved with +0.5 stops and Reinhard.
    prism::Recipe hand;
    prism::Recipe saved;
    for (const Example& e : examples()) {
        if (e.marker == "valid") {
            hand = prism::parse_recipe(e.text);
        } else if (e.marker == "saved") {
            saved = prism::parse_recipe(e.text);
        }
    }
    ASSERT_TRUE(saved.exposure && saved.curve);
    EXPECT_EQ(saved.exposure->stops, 0.5f);
    EXPECT_EQ(saved.curve->shape, prism::CurveShape::reinhard);
    EXPECT_EQ(saved.name, "Warm low contrast, brighter");
    saved.exposure.reset();
    saved.curve.reset();
    saved.name = hand.name;
    EXPECT_EQ(saved, hand);
}

TEST(RecipeDocs, EveryErrorExampleGivesItsLineAndMessage) {
    int count = 0;
    const std::string prefix = "error line ";
    for (const Example& e : examples()) {
        if (e.marker.rfind(prefix, 0) != 0) {
            continue;
        }
        ++count;
        const std::size_t colon = e.marker.find(": ");
        ASSERT_NE(colon, std::string::npos) << e.marker;
        const int line = std::stoi(e.marker.substr(prefix.size(), colon - prefix.size()));
        const std::string message = e.marker.substr(colon + 2);
        try {
            (void)prism::parse_recipe(e.text);
            ADD_FAILURE() << "reads, but should give: " << e.marker;
        } catch (const prism::RecipeError& error) {
            ASSERT_EQ(error.issues().size(), 1u) << error.what();
            EXPECT_EQ(error.issues()[0].line, line) << e.marker;
            EXPECT_EQ(error.issues()[0].message, message);
        }
    }
    EXPECT_GE(count, 5);
}

TEST(RecipeDocs, EveryMarkerIsOneTheTestsKnow) {
    for (const Example& e : examples()) {
        EXPECT_TRUE(e.marker == "valid" || e.marker == "saved" ||
                    e.marker.rfind("error line ", 0) == 0)
            << e.marker;
    }
}
