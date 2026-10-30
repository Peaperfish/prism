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
