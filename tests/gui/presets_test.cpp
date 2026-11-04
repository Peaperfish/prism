#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "prism/gui/presets.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/process.hpp"
#include "prism/recipe.hpp"

using prism::gui::EditSettings;
using prism::gui::InputKind;
using prism::gui::Preset;

namespace {

constexpr InputKind both_kinds[] = {InputKind::image, InputKind::raw};

// 16 x 4 pixels of varied colours from deep shadow to above white, so every preset changes it.
prism::Image make_image() {
    prism::Image img;
    img.width = 16;
    img.height = 4;
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const float level = 0.01f * static_cast<float>(1 << (x / 2));  // 0.01 to 1.28
            img.pixels.push_back(level * (y == 1 ? 0.3f : 1.0f));
            img.pixels.push_back(level * (y == 2 ? 0.3f : 0.8f));
            img.pixels.push_back(level * (y == 3 ? 0.3f : 0.6f));
        }
    }
    return img;
}

// The four stages of a look that LookTable accepts.
constexpr std::string_view stages =
    "[exposure]\nstops = +1.0\n[saturation]\namount = 1.0\n[contrast]\namount = 1.0\n"
    "[curve]\nshape = \"none\"\n";

std::string look_text(std::string_view body = stages) {
    return "prism_recipe = 1\nname = \"Test look\"\ndescription = \"For the test\"\n" +
           std::string(body);
}

// The messages LookTable gives for this one file; empty if it accepts it.
std::vector<prism::RecipeIssue> issues_of(const std::string& text) {
    const prism::gui::BuiltinLookFile files[] = {{"test", "raw/test.prism", text}};
    try {
        const prism::gui::LookTable table(files);
    } catch (const prism::RecipeError& e) {
        EXPECT_EQ(e.source(), "raw/test.prism");
        return e.issues();
    }
    return {};
}

bool has_message(const std::vector<prism::RecipeIssue>& issues, std::string_view words) {
    for (const prism::RecipeIssue& issue : issues) {
        if (issue.message.find(words) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// The 8-bit sRGB bytes a preset produces (what would end up in the file).
std::vector<std::uint8_t> render(const EditSettings& s) {
    prism::Image img = make_image();
    EXPECT_EQ(prism::process(img, prism::gui::to_process_settings(s), 1),
              prism::ProcessResult::completed);
    return prism::encode_srgb8(img, 1);
}

}  // namespace

TEST(Presets, EachLoadsAndValidates) {
    for (const InputKind kind : both_kinds) {
        ASSERT_FALSE(prism::gui::presets_for(kind).empty());
        for (const Preset& p : prism::gui::presets_for(kind)) {
            EditSettings s;
            prism::gui::apply_preset(s, p);
            EXPECT_TRUE(prism::gui::validate(s).empty()) << p.id;
            EXPECT_FALSE(p.name.empty() || p.description.empty()) << p.id;
            EXPECT_EQ(prism::gui::matching_preset(kind, s), &p) << p.id;
        }
    }
}

TEST(Presets, EachGivesADifferentImage) {
    for (const InputKind kind : both_kinds) {
        std::set<std::vector<std::uint8_t>> outputs;
        for (const Preset& p : prism::gui::presets_for(kind)) {
            EditSettings s;
            prism::gui::apply_preset(s, p);
            EXPECT_TRUE(outputs.insert(render(s)).second) << p.id << " looks like another preset";
        }
    }
}

TEST(Presets, IdsAreUniqueAndTheDefaultsAreAsPlanned) {
    for (const InputKind kind : both_kinds) {
        std::set<std::string_view> ids;
        for (const Preset& p : prism::gui::presets_for(kind)) {
            EXPECT_TRUE(ids.insert(p.id).second) << p.id;
        }
    }
    EXPECT_EQ(prism::gui::default_preset(InputKind::raw).id, "natural");
    EXPECT_EQ(prism::gui::default_preset(InputKind::image).id, "original");
    // Original on a PNG/JPEG changes nothing at all.
    EditSettings s;
    prism::gui::apply_preset(s, prism::gui::default_preset(InputKind::image));
    EXPECT_EQ(s, EditSettings{});
}

TEST(Presets, NoPngOrJpegPresetAddsACurve) {
    for (const Preset& p : prism::gui::presets_for(InputKind::image)) {
        EXPECT_EQ(p.settings.curve, prism::gui::CurveKind::none) << p.id;
    }
}

TEST(Presets, ApplyingOneKeepsTheMatrixAndRawOptions) {
    EditSettings s;
    s.matrix = prism::gui::MatrixFile{"grade.txt", {1, 0, 0, 0, 1, 0, 0, 0, 0.9f}};
    s.matrix_on = false;
    s.raw_half_size = true;
    s.raw_camera_matrix = prism::gui::MatrixFile{"cam.txt", prism::identity_matrix};
    s.saturation_on = false;
    const EditSettings before = s;

    prism::gui::apply_preset(s, prism::gui::presets_for(InputKind::raw)[2]);
    EXPECT_EQ(s.matrix, before.matrix);
    EXPECT_EQ(s.matrix_on, before.matrix_on);
    EXPECT_EQ(s.raw_half_size, before.raw_half_size);
    EXPECT_EQ(s.raw_camera_matrix, before.raw_camera_matrix);
    EXPECT_TRUE(s.saturation_on) << "a preset turns its own stages on";
}

TEST(Presets, AnyChangeToTheLookIsCustom) {
    EditSettings s;
    const Preset& vivid = prism::gui::presets_for(InputKind::raw)[2];
    ASSERT_EQ(vivid.id, "vivid");
    prism::gui::apply_preset(s, vivid);

    s.saturation += 0.01f;
    EXPECT_EQ(prism::gui::matching_preset(InputKind::raw, s), nullptr);
    s.saturation = vivid.settings.saturation;
    s.curve_on = false;
    EXPECT_EQ(prism::gui::matching_preset(InputKind::raw, s), nullptr);
    s.curve_on = true;
    EXPECT_EQ(prism::gui::matching_preset(InputKind::raw, s), &vivid);
}

TEST(Presets, OnlyTheEffectiveLookCounts) {
    EditSettings s;
    const Preset& natural = prism::gui::default_preset(InputKind::raw);
    prism::gui::apply_preset(s, natural);

    // Not part of the look: still Natural.
    s.gamma = 1.4f;  // filmic is chosen, so gamma's value does nothing
    s.matrix = prism::gui::MatrixFile{"grade.txt", prism::identity_matrix};
    s.raw_half_size = true;
    s.saturation = 4.0f;  // ...because saturation is off:
    s.saturation_on = false;
    EXPECT_EQ(prism::gui::matching_preset(InputKind::raw, s), &natural);

    // An off stage counts as neutral, so Original matches a file with every look stage off.
    EditSettings off;
    off.exposure_stops = 2.0f;
    off.curve = prism::gui::CurveKind::filmic;
    off.exposure_on = off.curve_on = false;
    EXPECT_EQ(prism::gui::matching_preset(InputKind::raw, off)->id, "original");
}

// --- Reading looks -------------------------------------------------------------------------------

TEST(LookTable, AGoodFileIsALook) {
    const std::string text = look_text();
    const prism::gui::BuiltinLookFile files[] = {{"test", "raw/test.prism", text}};
    const prism::gui::LookTable table(files);
    ASSERT_EQ(table.presets().size(), 1u);
    EXPECT_EQ(table.presets()[0].id, "test");
    EXPECT_EQ(table.presets()[0].name, "Test look");
    EXPECT_EQ(table.presets()[0].description, "For the test");
    EXPECT_EQ(table.presets()[0].settings.exposure_stops, 1.0f);
}

TEST(LookTable, AFileThatDoesNotReadIsRefusedByName) {
    const std::vector<prism::RecipeIssue> issues = issues_of("prism_recipe = 1\nnmae = \"x\"\n");
    ASSERT_FALSE(issues.empty());
    EXPECT_EQ(issues[0].line, 2);
    EXPECT_TRUE(has_message(issues, "Did you mean 'name'?"));
    EXPECT_TRUE(has_message(issues_of(""), "no settings"));
}

TEST(LookTable, ALookHasExactlyItsFourStagesAllOn) {
    EXPECT_TRUE(issues_of(look_text()).empty());
    const std::string matrix =
        "[matrix]\nred = [1, 0, 0]\ngreen = [0, 1, 0]\nblue = [0, 0, 1]\n";
    EXPECT_TRUE(has_message(issues_of(look_text(std::string(stages) + matrix)), "[matrix]"));

    for (const std::string_view stage : {"exposure", "saturation", "contrast", "curve"}) {
        const std::string section = "[" + std::string(stage) + "]";
        const std::string header = section + "\n";
        std::string body(stages);

        // Left out.
        std::string without = body;
        const std::size_t at = without.find(header);
        ASSERT_NE(at, std::string::npos);
        const std::size_t next = without.find('[', at + 1);
        without.erase(at, next == std::string::npos ? std::string::npos : next - at);
        EXPECT_TRUE(has_message(issues_of(look_text(without)), "must have " + section)) << stage;

        // Switched off.
        body.insert(body.find('\n', at + header.size()) + 1, "on = false\n");
        EXPECT_TRUE(has_message(issues_of(look_text(body)), "switch off " + section)) << stage;
    }
}

TEST(LookTable, ALookHasANameAndADescription) {
    const std::string body(stages);
    const auto issues = [&](std::string_view top) {
        return issues_of("prism_recipe = 1\n" + std::string(top) + body);
    };
    EXPECT_TRUE(has_message(issues("description = \"d\"\n"), "must have a name"));
    EXPECT_TRUE(has_message(issues("name = \"\"\ndescription = \"d\"\n"), "must have a name"));
    EXPECT_TRUE(has_message(issues("name = \"n\"\n"), "must have a description"));
    EXPECT_TRUE(
        has_message(issues("name = \"n\"\ndescription = \"\"\n"), "must have a description"));
}

// Short names sit inside the std::string, so these point into the table's own storage. Under
// AddressSanitizer a view left pointing at the input, or at moved-from text, fails here.
TEST(LookTable, ThePresetsPointIntoTheTablesOwnStorage) {
    std::vector<std::string> ids;
    std::vector<std::string> texts;
    std::vector<prism::gui::BuiltinLookFile> files;
    for (int i = 0; i < 40; ++i) {
        ids.push_back("id" + std::to_string(i));
        texts.push_back("prism_recipe = 1\nname = \"n" + std::to_string(i) +
                        "\"\ndescription = \"A description long enough to be on the heap\"\n" +
                        std::string(stages));
    }
    for (int i = 0; i < 40; ++i) {
        files.push_back({ids[i], "raw/test.prism", texts[i]});
    }
    const prism::gui::LookTable table(files);
    files.clear();
    ids.clear();
    texts.clear();
    ASSERT_EQ(table.presets().size(), 40u);
    for (int i = 0; i < 40; ++i) {
        EXPECT_EQ(table.presets()[i].id, "id" + std::to_string(i));
        EXPECT_EQ(table.presets()[i].name, "n" + std::to_string(i));
        EXPECT_EQ(table.presets()[i].description, "A description long enough to be on the heap");
    }
}
