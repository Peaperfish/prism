#include <gtest/gtest.h>

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "run_command.hpp"

// Keeps docs/CLI.md in step with the tools' own --help: every option the help text names must
// have an entry in CLI.md, and CLI.md must not document an option the help does not name.
//
// In the help text, an option is a word that starts with one or two dashes and a letter, and
//   * stands at the start of a line (the option lists: "  --exposure <stops>  ...",
//     "  -h, --help  ..."; several, separated by commas, may share a line), or
//   * is in the "Usage:" line (so "-o", which the option list of `prism` does not repeat).
// Words inside a sentence (such as "prism --raw-camera-matrix", in prism_profile's help) are not
// options of that tool and are ignored.
//
// In CLI.md, an option is documented by a heading ("### `--exposure <stops>`", or
// "### `-h`, `--help`") in the section of its tool: "## Options of prism" or "## prism_profile".

namespace fs = std::filesystem;

namespace {

std::string read_text(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

bool is_option_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-';
}

// The option starting at text[pos] ("-o", "--exposure"), or "" if there is none there.
std::string option_at(const std::string& text, std::size_t pos) {
    std::size_t end = pos;
    while (end < text.size() && text[end] == '-' && end - pos < 2) {
        ++end;
    }
    if (end == pos || end >= text.size() ||
        std::isalpha(static_cast<unsigned char>(text[end])) == 0) {
        return "";
    }
    while (end < text.size() && is_option_char(text[end])) {
        ++end;
    }
    return text.substr(pos, end - pos);
}

std::set<std::string> options_in_help(const std::string& help) {
    std::set<std::string> options;
    for (const std::string& line : lines_of(help)) {
        std::size_t pos = line.find_first_not_of(' ');
        if (pos == std::string::npos) {
            continue;
        }
        if (line.compare(pos, 6, "Usage:") == 0) {
            for (std::size_t i = pos; i < line.size(); ++i) {
                // A dash that begins a word: after a space or a bracket.
                if (line[i] == '-' && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '[')) {
                    const std::string option = option_at(line, i);
                    if (!option.empty()) {
                        options.insert(option);
                        i += option.size() - 1;
                    }
                }
            }
            continue;
        }
        // Leading options, "-h, --help".
        while (pos < line.size()) {
            const std::string option = option_at(line, pos);
            if (option.empty()) {
                break;
            }
            options.insert(option);
            pos += option.size();
            if (line.compare(pos, 2, ", ") != 0) {
                break;
            }
            pos += 2;
        }
    }
    return options;
}

// The lines of the "## <name>" section of CLI.md, up to the next "## " heading.
std::vector<std::string> section_of(const std::string& doc, const std::string& name) {
    std::vector<std::string> section;
    bool inside = false;
    for (const std::string& line : lines_of(doc)) {
        if (line.rfind("## ", 0) == 0) {
            inside = line == "## " + name;
            continue;
        }
        if (inside) {
            section.push_back(line);
        }
    }
    return section;
}

// The options named in the section's "### " headings, inside backticks.
std::set<std::string> options_in_headings(const std::vector<std::string>& section) {
    std::set<std::string> options;
    for (const std::string& line : section) {
        if (line.rfind("### ", 0) != 0) {
            continue;
        }
        std::size_t open = line.find('`');
        while (open != std::string::npos) {
            const std::size_t close = line.find('`', open + 1);
            if (close == std::string::npos) {
                break;
            }
            const std::string option = option_at(line, open + 1);
            if (!option.empty()) {
                options.insert(option);
            }
            open = line.find('`', close + 1);
        }
    }
    return options;
}

std::string help_of(const char* exe) {
    const fs::path log =
        fs::path(testing::TempDir()) /
        ("prism_cli_docs_" +
         std::string(testing::UnitTest::GetInstance()->current_test_info()->name()) + ".txt");
    const int code = prism_test::run_command("\"" + std::string(exe) + "\" --help > \"" +
                                             log.string() + "\" 2>&1");
    const std::string text = read_text(log);
    fs::remove(log);
    EXPECT_EQ(code, 0) << text;
    return text;
}

void expect_same_options(const std::string& exe, const std::string& section_name) {
    const std::set<std::string> in_help = options_in_help(help_of(exe.c_str()));
    const std::string doc = read_text(PRISM_CLI_DOC);
    ASSERT_FALSE(doc.empty()) << "cannot read " << PRISM_CLI_DOC;
    const std::set<std::string> in_doc = options_in_headings(section_of(doc, section_name));

    // A parser that found nothing would make the checks below pass for the wrong reason.
    ASSERT_GE(in_help.size(), 3u) << "found too few options in --help";
    ASSERT_GE(in_doc.size(), 3u) << "found too few option headings in the '" << section_name
                                 << "' section of CLI.md";

    for (const std::string& option : in_help) {
        EXPECT_TRUE(in_doc.count(option) != 0)
            << option << " is in " << section_name << " --help but not documented in CLI.md";
    }
    for (const std::string& option : in_doc) {
        EXPECT_TRUE(in_help.count(option) != 0)
            << option << " is documented in CLI.md but not in " << section_name << " --help";
    }
}

}  // namespace

TEST(CliDocs, PrismOptionsMatchHelp) {
    expect_same_options(PRISM_EXE, "Options of prism");
}

TEST(CliDocs, PrismProfileOptionsMatchHelp) {
    expect_same_options(PRISM_PROFILE_EXE, "prism_profile");
}

// The parser itself, on a fixed help text.
TEST(CliDocs, ParserReadsLeadingOptionsAndTheUsageLine) {
    const std::string help =
        "Usage: prism <input> [-o <output>] [--histogram <file>] [options]\n"
        "At least one of -x and --nope is required, for prism --elsewhere.\n"
        "  --exposure <stops>  brighten (+) or darken (-) by this many stops\n"
        "  -h, --help          show this help\n"
        "                      single-threaded; --not-this either\n";
    const std::set<std::string> expected{"-o", "--histogram", "--exposure", "-h", "--help"};
    EXPECT_EQ(options_in_help(help), expected);
}

TEST(CliDocs, RecipesAndBatchesExamplesParseWithKnownOptions) {
    const std::set<std::string> known = options_in_help(help_of(PRISM_EXE));
    const std::string doc = read_text(PRISM_CLI_DOC);
    ASSERT_FALSE(doc.empty()) << "cannot read " << PRISM_CLI_DOC;
    const std::vector<std::string> lines = section_of(doc, "Recipes and batches");
    ASSERT_FALSE(lines.empty()) << "missing '## Recipes and batches' section in CLI.md";

    bool in_sh = false;
    int commands = 0;
    for (const std::string& line : lines) {
        if (line == "```sh") {
            in_sh = true;
            continue;
        }
        if (line == "```") {
            in_sh = false;
            continue;
        }
        if (!in_sh || line.empty() || line[0] == '#') {
            continue;
        }
        ++commands;
        std::istringstream in(line);
        std::vector<std::string> words;
        for (std::string w; in >> w;) {
            words.push_back(w);
        }
        ASSERT_FALSE(words.empty()) << line;
        EXPECT_EQ(words[0], "prism") << line;

        bool has_recipe = false;
        bool has_output = false;
        std::size_t inputs = 0;
        for (std::size_t i = 1; i < words.size(); ++i) {
            const std::string& w = words[i];
            if (w[0] == '-') {
                EXPECT_TRUE(known.count(w) != 0)
                    << "unknown option '" << w << "' in example: " << line;
                if (w == "--recipe") {
                    has_recipe = true;
                }
                if (w == "-o" || w == "--out-dir" || w == "--histogram") {
                    has_output = true;
                }
                if (w != "-h" && w != "--help" && w != "--version" && w != "--time" &&
                    w != "--raw-half") {
                    ASSERT_LT(i + 1, words.size())
                        << "missing value for '" << w << "' in example: " << line;
                    ++i;  // skip the option's value (even if it starts with '-' like -1.25)
                }
            } else {
                ++inputs;
            }
        }
        EXPECT_TRUE(has_recipe) << line;
        EXPECT_TRUE(has_output) << line;
        EXPECT_GE(inputs, 1u) << line;
    }
    EXPECT_GE(commands, 3) << "expected at least 3 worked examples in '## Recipes and batches'";
}
