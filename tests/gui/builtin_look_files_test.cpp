#include "prism/gui/builtin_looks.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

// The embedded recipes against the files in recipes/.

namespace {

namespace fs = std::filesystem;
using prism::gui::BuiltinLookFile;

struct Folder {
    std::string_view name;
    std::span<const BuiltinLookFile> files;
};

std::set<fs::path> embedded_stems(std::span<const BuiltinLookFile> files) {
    std::set<fs::path> stems;
    for (const BuiltinLookFile& f : files) {
        stems.insert(fs::path(f.id));
    }
    return stems;
}

std::string without_carriage_returns(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    EXPECT_TRUE(file.is_open()) << path;
    std::stringstream text;
    text << file.rdbuf();
    std::string out;
    for (const char c : text.str()) {
        if (c != '\r') {
            out += c;
        }
    }
    return out;
}

}  // namespace

TEST(BuiltinLookFiles, TheEmbeddedTextIsTheFilesText) {
    const fs::path root(PRISM_RECIPES_DIR);
    for (const Folder& folder : {Folder{"raw", prism::gui::builtin_raw_look_files()},
                                 Folder{"image", prism::gui::builtin_image_look_files()}}) {
        ASSERT_FALSE(folder.files.empty()) << folder.name;
        for (const BuiltinLookFile& f : folder.files) {
            const fs::path path =
                root / fs::path(folder.name) / fs::path(std::string(f.id) + ".prism");
            EXPECT_EQ(f.text, without_carriage_returns(path)) << f.source;
        }
    }
}

TEST(BuiltinLookFiles, TheTextHasNoCarriageReturnOrByteOrderMark) {
    for (const std::span<const BuiltinLookFile> files :
         {prism::gui::builtin_raw_look_files(), prism::gui::builtin_image_look_files()}) {
        for (const BuiltinLookFile& f : files) {
            EXPECT_EQ(f.text.find('\r'), std::string_view::npos) << f.source;
            EXPECT_NE(f.text.substr(0, 3), "\xEF\xBB\xBF") << f.source;
            EXPECT_EQ(f.text.find('\0'), std::string_view::npos) << f.source;
            EXPECT_EQ(f.text.substr(0, 1), "#") << f.source;
            EXPECT_EQ(f.text.back(), '\n') << f.source;
        }
    }
}

TEST(BuiltinLookFiles, EveryPrismFileInRecipesIsInAList) {
    std::set<fs::path> raw;
    std::set<fs::path> image;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(PRISM_RECIPES_DIR)) {
        if (!entry.is_regular_file() || entry.path().extension() != fs::path(".prism")) {
            continue;
        }
        const fs::path folder = entry.path().parent_path().filename();
        if (folder == fs::path("raw")) {
            raw.insert(entry.path().stem());
        } else if (folder == fs::path("image")) {
            image.insert(entry.path().stem());
        } else {
            ADD_FAILURE() << entry.path() << " is neither in recipes/raw nor in recipes/image";
        }
    }
    EXPECT_EQ(raw, embedded_stems(prism::gui::builtin_raw_look_files()));
    EXPECT_EQ(image, embedded_stems(prism::gui::builtin_image_look_files()));
}
