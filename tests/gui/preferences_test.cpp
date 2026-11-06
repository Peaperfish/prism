#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "prism/gui/preferences.hpp"
#include "prism/utf8_path.hpp"

namespace fs = std::filesystem;
using prism::gui::Preferences;
using prism::gui::SystemTheme;
using prism::gui::Theme;
using prism::gui::ThemeChoice;

namespace {

fs::path temp_file(const std::string& name) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    return fs::path(testing::TempDir()) /
           ("prism_prefs_" + std::string(test->name()) + "_" + std::to_string(pid) + "_" + name);
}

void write_text(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

Preferences customised() {
    Preferences p;
    p.theme = ThemeChoice::light;
    p.interface_size = 150;
    p.panel_width = 400;
    p.scope = prism::gui::ScopeChoice::vectorscope;
    p.clip_overlay = true;
    p.show_explanations = false;
    return p;
}

TEST(StepInterfaceSize, MovesThroughThePresetsAndStopsAtTheEnds) {
    using prism::gui::step_interface_size;
    EXPECT_EQ(step_interface_size(100, +1, 1.0f), 125);
    EXPECT_EQ(step_interface_size(100, -1, 1.0f), 75);
    EXPECT_EQ(step_interface_size(125, -1, 1.0f), 100);
    EXPECT_EQ(step_interface_size(175, +1, 1.0f), 200);
    EXPECT_EQ(step_interface_size(200, +1, 1.0f), 200);
    EXPECT_EQ(step_interface_size(75, -1, 1.0f), 75);
    EXPECT_EQ(step_interface_size(100, 0, 1.0f), 100);
    // Walking the whole way up and down visits every preset once.
    int size = 75;
    for (int expected = 100; expected <= 200; expected += 25) {
        size = step_interface_size(size, +1, 1.0f);
        EXPECT_EQ(size, expected);
    }
    for (int expected = 175; expected >= 75; expected -= 25) {
        size = step_interface_size(size, -1, 1.0f);
        EXPECT_EQ(size, expected);
    }
}

TEST(StepInterfaceSize, ASizeOffTheLadderIsSnappedFirst) {
    using prism::gui::step_interface_size;
    EXPECT_EQ(step_interface_size(110, +1, 1.0f), 125);  // 110 is read as 100
    EXPECT_EQ(step_interface_size(500, -1, 1.0f), 175);  // 500 is read as 200
    EXPECT_EQ(step_interface_size(10, -1, 1.0f), 75);
}

TEST(StepInterfaceSize, AutoSitsAtTheDisplaysScaleOnTheLadder) {
    using prism::gui::interface_size_auto;
    using prism::gui::step_interface_size;
    // On a preset: the neighbours.
    EXPECT_EQ(step_interface_size(interface_size_auto, +1, 1.0f), 125);
    EXPECT_EQ(step_interface_size(interface_size_auto, -1, 1.0f), 75);
    EXPECT_EQ(step_interface_size(interface_size_auto, +1, 1.5f), 175);
    EXPECT_EQ(step_interface_size(interface_size_auto, -1, 1.5f), 125);
    // Between two presets (a 110% display): the ones either side.
    EXPECT_EQ(step_interface_size(interface_size_auto, +1, 1.1f), 125);
    EXPECT_EQ(step_interface_size(interface_size_auto, -1, 1.1f), 100);
    // Rounding noise on a preset doesn't count as being past it.
    EXPECT_EQ(step_interface_size(interface_size_auto, +1, 1.2500001f), 150);
    EXPECT_EQ(step_interface_size(interface_size_auto, -1, 1.2499999f), 100);
    // A display scale that is no usable number counts as 100%.
    EXPECT_EQ(step_interface_size(interface_size_auto, +1, 0.0f), 125);
}

TEST(StepInterfaceSize, AutoBeyondTheEndsStaysAutoThatWay) {
    using prism::gui::interface_size_auto;
    using prism::gui::step_interface_size;
    EXPECT_EQ(step_interface_size(interface_size_auto, +1, 2.0f), interface_size_auto);
    EXPECT_EQ(step_interface_size(interface_size_auto, +1, 3.0f), interface_size_auto);
    EXPECT_EQ(step_interface_size(interface_size_auto, -1, 3.0f), 200);
    EXPECT_EQ(step_interface_size(interface_size_auto, -1, 0.75f), interface_size_auto);
    EXPECT_EQ(step_interface_size(interface_size_auto, -1, 0.5f), interface_size_auto);
    EXPECT_EQ(step_interface_size(interface_size_auto, +1, 0.5f), 75);
}

}  // namespace

TEST(Preferences, DefaultsAreSystemThemeAutoSizeAndTheStandardPanel) {
    const Preferences p;
    EXPECT_EQ(p.theme, ThemeChoice::system);
    EXPECT_EQ(p.interface_size, prism::gui::interface_size_auto);
    EXPECT_EQ(p.panel_width, 440);
    EXPECT_TRUE(p.unknown.empty());
}

TEST(Preferences, TheScopeIsRememberedAndABadValueFallsBackToTheHistogram) {
    using prism::gui::ScopeChoice;
    EXPECT_EQ(Preferences{}.scope, ScopeChoice::histogram);
    for (const ScopeChoice c : {ScopeChoice::histogram, ScopeChoice::parade, ScopeChoice::waveform,
                                ScopeChoice::vectorscope}) {
        Preferences p;
        p.scope = c;
        EXPECT_EQ(prism::gui::parse_preferences(prism::gui::serialize_preferences(p)).scope, c);
    }
    EXPECT_EQ(prism::gui::parse_preferences("scope=radar\n").scope, ScopeChoice::histogram);
    EXPECT_TRUE(prism::gui::parse_preferences("scope=parade\n").unknown.empty());
}

TEST(Preferences, SerializeAndParseRoundTrip) {
    const Preferences p = customised();
    const std::string text = prism::gui::serialize_preferences(p);
    EXPECT_EQ(text,
              "# Prism preferences\ntheme=light\ninterface_size=150\npanel_width=400\n"
              "scope=vectorscope\nclip_overlay=on\nshow_explanations=off\n");
    EXPECT_EQ(prism::gui::parse_preferences(text), p);
    EXPECT_EQ(prism::gui::parse_preferences(prism::gui::serialize_preferences(Preferences{})),
              Preferences{});
}

TEST(Preferences, TheClippingOverlayIsOffByDefaultAndRemembered) {
    EXPECT_FALSE(Preferences{}.clip_overlay);
    EXPECT_TRUE(prism::gui::parse_preferences("clip_overlay=on\n").clip_overlay);
    EXPECT_FALSE(prism::gui::parse_preferences("clip_overlay=off\n").clip_overlay);
    EXPECT_FALSE(prism::gui::parse_preferences("clip_overlay=maybe\n").clip_overlay);
    EXPECT_TRUE(prism::gui::parse_preferences("clip_overlay=on\n").unknown.empty());
}

TEST(Preferences, ExplanationsAreOnByDefaultAndTheChoiceIsRemembered) {
    EXPECT_TRUE(Preferences{}.show_explanations);
    EXPECT_TRUE(prism::gui::parse_preferences("").show_explanations);
    EXPECT_FALSE(prism::gui::parse_preferences("show_explanations=off\n").show_explanations);
    EXPECT_TRUE(prism::gui::parse_preferences("show_explanations=on\n").show_explanations);
    EXPECT_TRUE(prism::gui::parse_preferences("show_explanations=maybe\n").show_explanations);
    EXPECT_TRUE(prism::gui::parse_preferences("show_explanations=off\n").unknown.empty());
    Preferences off;
    off.show_explanations = false;
    EXPECT_FALSE(
        prism::gui::parse_preferences(prism::gui::serialize_preferences(off)).show_explanations);
    EXPECT_NE(prism::gui::serialize_preferences(Preferences{}).find("show_explanations=on\n"),
              std::string::npos);
}

TEST(Preferences, TheRecipeFolderIsRememberedAfterTheOtherKeys) {
    Preferences p = customised();
    p.recipe_folder = prism::path_from_utf8("/Users/me/Looks");
    const std::string text = prism::gui::serialize_preferences(p);
    EXPECT_NE(text.find("show_explanations=off\nrecipe_folder=/Users/me/Looks\n"),
              std::string::npos);
    EXPECT_EQ(prism::gui::parse_preferences(text), p);
}

TEST(Preferences, ANonAsciiRecipeFolderRoundTrips) {
    Preferences p;
    p.recipe_folder = prism::path_from_utf8("/Users/me/Rezepte für Fotos/色");
    const Preferences back = prism::gui::parse_preferences(prism::gui::serialize_preferences(p));
    EXPECT_EQ(back.recipe_folder, p.recipe_folder);
    EXPECT_EQ(prism::utf8(back.recipe_folder), "/Users/me/Rezepte für Fotos/色");
}

TEST(Preferences, NoRecipeFolderIsWrittenOrReadWhenThereIsNone) {
    EXPECT_TRUE(Preferences{}.recipe_folder.empty());
    EXPECT_EQ(prism::gui::serialize_preferences(Preferences{}).find("recipe_folder"),
              std::string::npos);
    EXPECT_TRUE(prism::gui::parse_preferences("theme=dark\n").recipe_folder.empty());
    EXPECT_TRUE(prism::gui::parse_preferences("recipe_folder=\n").recipe_folder.empty());
}

TEST(Preferences, SaveAndLoadRoundTripThroughANonAsciiPath) {
    fs::path utf8_path = temp_file("prefs");
    utf8_path += prism::path_from_utf8("_préférences_写真.txt");
    ASSERT_EQ(prism::gui::save_preferences(utf8_path, customised()), std::nullopt);
    EXPECT_EQ(read_text(utf8_path), prism::gui::serialize_preferences(customised()));
    EXPECT_EQ(prism::gui::load_preferences(utf8_path), customised());
    EXPECT_FALSE(fs::exists(fs::path(utf8_path) += ".tmp")) << "the temporary file is renamed";

    // Saving again replaces the file.
    ASSERT_EQ(prism::gui::save_preferences(utf8_path, Preferences{}), std::nullopt);
    EXPECT_EQ(prism::gui::load_preferences(utf8_path), Preferences{});
    fs::remove(utf8_path);
}

TEST(Preferences, AMissingFileGivesTheDefaults) {
    EXPECT_EQ(prism::gui::load_preferences(temp_file("never_written.txt")), Preferences{});
}

TEST(Preferences, SavingIntoAMissingFolderReportsAnError) {
    const fs::path path = temp_file("no_such_folder") / "prefs.txt";
    const std::optional<std::string> error = prism::gui::save_preferences(path, customised());
    ASSERT_TRUE(error);
    EXPECT_NE(error->find("prefs.txt"), std::string::npos) << *error;
}

TEST(Preferences, ACorruptFileFallsBackPerKey) {
    // Binary junk, a line without '=', and bad values for two keys; the good key still counts.
    const std::string text = std::string("\x89PNG\r\n\x1a\n\0\0junk", 14) +
                             "\nthis line has no equals sign\n"
                             "theme=purple\n"
                             "interface_size=big\n"
                             "panel_width=400\n";
    Preferences expected;
    expected.panel_width = 400;
    EXPECT_EQ(prism::gui::parse_preferences(text), expected);

    const fs::path path = temp_file("corrupt.txt");
    write_text(path, text);
    EXPECT_EQ(prism::gui::load_preferences(path), expected);
    fs::remove(path);
}

TEST(Preferences, MalformedNumbersAndEmptyValuesGiveDefaults) {
    const Preferences p = prism::gui::parse_preferences(
        "interface_size=150%\npanel_width=320px\ntheme=\n=orphan value\n");
    EXPECT_EQ(p.interface_size, prism::gui::interface_size_auto);
    EXPECT_EQ(p.panel_width, 440);
    EXPECT_EQ(p.theme, ThemeChoice::system);
    EXPECT_TRUE(p.unknown.empty()) << "a line without a key is dropped";
}

TEST(Preferences, WhitespaceCommentsAndWindowsLineEndingsAreFine) {
    const Preferences p = prism::gui::parse_preferences(
        "# comment\r\n\r\n  theme = dark \r\ninterface_size=auto\r\npanel_width= 500\r\n");
    EXPECT_EQ(p.theme, ThemeChoice::dark);
    EXPECT_EQ(p.interface_size, prism::gui::interface_size_auto);
    EXPECT_EQ(p.panel_width, 500);
}

TEST(Preferences, UnknownKeysAreKeptAndWrittenBack) {
    const Preferences p = prism::gui::parse_preferences(
        "theme=dark\nfuture_option=on\npanel_width=300\nzoom=fit\n");
    ASSERT_EQ(p.unknown.size(), 2u);
    EXPECT_EQ(p.unknown[0], (std::pair<std::string, std::string>{"future_option", "on"}));
    EXPECT_EQ(p.unknown[1], (std::pair<std::string, std::string>{"zoom", "fit"}));
    EXPECT_EQ(p.theme, ThemeChoice::dark);
    EXPECT_EQ(p.panel_width, 300);

    const std::string text = prism::gui::serialize_preferences(p);
    EXPECT_NE(text.find("future_option=on\n"), std::string::npos);
    EXPECT_NE(text.find("zoom=fit\n"), std::string::npos);
    EXPECT_EQ(prism::gui::parse_preferences(text), p);
}

TEST(Preferences, OutOfRangeValuesAreClamped) {
    Preferences p = prism::gui::parse_preferences("interface_size=500\npanel_width=10\n");
    EXPECT_EQ(p.interface_size, 200);
    EXPECT_EQ(p.panel_width, prism::gui::panel_width_min);

    p = prism::gui::parse_preferences("interface_size=10\npanel_width=99999\n");
    EXPECT_EQ(p.interface_size, 75);
    EXPECT_EQ(p.panel_width, prism::gui::panel_width_max);

    p = prism::gui::parse_preferences("interface_size=-100\n");
    EXPECT_EQ(p.interface_size, 75);
    p = prism::gui::parse_preferences("interface_size=99999999999999999999\n");
    EXPECT_EQ(p.interface_size, prism::gui::interface_size_auto) << "doesn't fit an int: default";
}

TEST(Preferences, InterfaceSizeSnapsToSteps) {
    EXPECT_EQ(prism::gui::clamp_interface_size(0), 0) << "auto stays auto";
    EXPECT_EQ(prism::gui::clamp_interface_size(75), 75);
    EXPECT_EQ(prism::gui::clamp_interface_size(87), 75);
    EXPECT_EQ(prism::gui::clamp_interface_size(88), 100);
    EXPECT_EQ(prism::gui::clamp_interface_size(110), 100);
    EXPECT_EQ(prism::gui::clamp_interface_size(140), 150);
    EXPECT_EQ(prism::gui::clamp_interface_size(200), 200);
    EXPECT_EQ(prism::gui::clamp_interface_size(201), 200);
    for (int percent = -50; percent <= 300; ++percent) {
        const int size = prism::gui::clamp_interface_size(percent);
        if (size != 0) {
            EXPECT_GE(size, 75);
            EXPECT_LE(size, 200);
            EXPECT_EQ(size % 25, 0) << percent;
        }
    }
}

TEST(Preferences, InterfaceScaleFollowsTheDisplayOnlyOnAuto) {
    Preferences p;
    EXPECT_FLOAT_EQ(prism::gui::interface_scale(p, 1.5f), 1.5f);
    EXPECT_FLOAT_EQ(prism::gui::interface_scale(p, 0.0f), 1.0f) << "no display scale known";
    p.interface_size = 125;
    EXPECT_FLOAT_EQ(prism::gui::interface_scale(p, 2.0f), 1.25f);
    p.interface_size = 1000;  // not clamped yet, e.g. set in code
    EXPECT_FLOAT_EQ(prism::gui::interface_scale(p, 1.0f), 2.0f);
}

TEST(Preferences, ThemeResolution) {
    using prism::gui::resolve_theme;
    EXPECT_EQ(resolve_theme(ThemeChoice::system, SystemTheme::dark), Theme::dark);
    EXPECT_EQ(resolve_theme(ThemeChoice::system, SystemTheme::light), Theme::light);
    EXPECT_EQ(resolve_theme(ThemeChoice::system, SystemTheme::unknown), Theme::dark);
    for (SystemTheme os : {SystemTheme::unknown, SystemTheme::light, SystemTheme::dark}) {
        EXPECT_EQ(resolve_theme(ThemeChoice::light, os), Theme::light);
        EXPECT_EQ(resolve_theme(ThemeChoice::dark, os), Theme::dark);
    }
}
