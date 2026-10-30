#include "prism/gui/presets.hpp"

#include <vector>

namespace prism::gui {

namespace {

// A look: the fields a preset sets. Everything else stays at EditSettings' defaults.
EditSettings look(float exposure, float saturation, float contrast, CurveKind curve) {
    EditSettings s;
    s.exposure_stops = exposure;
    s.saturation = saturation;
    s.contrast = contrast;
    s.curve = curve;
    return s;
}

// Initial values, from the GUI plan (section 7). They still need checking on real photos (an
// a7R V RAW, a few phone JPEGs) and may be tuned later (plan decision D17). Every value is one
// the CLI accepts, so each preset is also a prism command line.
//
// RAW files are linear: without a curve they look dark and flat, so every RAW preset except
// Original brightens by a stop and adds a curve. +1 stop with filmic is the README's starting
// point for an a7R V file, and Vivid is the README's "closer to the camera's JPEG" example.
const std::vector<Preset>& raw_presets() {
    static const std::vector<Preset> presets = {
        {"natural", "Natural", "A stop brighter with a filmic curve: a neutral starting point",
         look(1.0f, 1.0f, 1.0f, CurveKind::filmic)},
        {"original", "Original", "No adjustments: the linear image as the sensor saw it",
         look(0.0f, 1.0f, 1.0f, CurveKind::none)},
        {"vivid", "Vivid", "More colour and contrast, closer to a camera JPEG",
         look(1.0f, 1.25f, 1.15f, CurveKind::filmic)},
        // Reinhard rolls the highlights off more gently than filmic; with less contrast and
        // colour that gives a low-key, soft look.
        {"soft", "Soft", "Gentle contrast, muted colour and soft highlights",
         look(1.0f, 0.9f, 0.85f, CurveKind::reinhard)},
        // A little extra contrast, because a greyscale image without colour contrast looks flat.
        {"bw", "Black & white", "Greyscale with a little extra contrast",
         look(1.0f, 0.0f, 1.2f, CurveKind::filmic)},
    };
    return presets;
}

// A PNG or JPEG already has a tone curve and the camera's colour: no preset adds a curve (that
// would apply one twice and dull the whites), and the changes are milder than for RAW.
const std::vector<Preset>& image_presets() {
    static const std::vector<Preset> presets = {
        {"original", "Original", "No adjustments: the photo as it is",
         look(0.0f, 1.0f, 1.0f, CurveKind::none)},
        {"vivid", "Vivid", "More colour and contrast", look(0.0f, 1.2f, 1.1f, CurveKind::none)},
        {"soft", "Soft", "Gentler contrast and muted colour",
         look(0.0f, 0.9f, 0.9f, CurveKind::none)},
        {"bw", "Black & white", "Greyscale with a little extra contrast",
         look(0.0f, 0.0f, 1.15f, CurveKind::none)},
    };
    return presets;
}

// The look as it acts on the image: an off stage is its neutral value.
struct EffectiveLook {
    float exposure;
    float saturation;
    float contrast;
    CurveKind curve;
    float curve_parameter;  // gamma or Reinhard white; 0 for the curves without one

    bool operator==(const EffectiveLook&) const = default;
};

EffectiveLook effective(const EditSettings& s) {
    const CurveKind curve = s.curve_on ? s.curve : CurveKind::none;
    float parameter = 0.0f;
    if (curve == CurveKind::gamma) {
        parameter = s.gamma;
    } else if (curve == CurveKind::reinhard) {
        parameter = s.reinhard_white;
    }
    return {s.exposure_on ? s.exposure_stops : 0.0f, s.saturation_on ? s.saturation : 1.0f,
            s.contrast_on ? s.contrast : 1.0f, curve, parameter};
}

}  // namespace

std::span<const Preset> presets_for(InputKind kind) {
    return kind == InputKind::raw ? raw_presets() : image_presets();
}

const Preset& default_preset(InputKind kind) {
    return presets_for(kind).front();
}

void apply_preset(EditSettings& s, const Preset& preset) {
    const EditSettings& p = preset.settings;
    s.exposure_stops = p.exposure_stops;
    s.saturation = p.saturation;
    s.contrast = p.contrast;
    s.curve = p.curve;
    s.gamma = p.gamma;
    s.reinhard_white = p.reinhard_white;
    s.exposure_on = s.saturation_on = s.contrast_on = s.curve_on = true;
}

bool same_look(const EditSettings& a, const EditSettings& b) {
    return effective(a) == effective(b);
}

const Preset* matching_preset(InputKind kind, const EditSettings& s) {
    for (const Preset& p : presets_for(kind)) {
        if (same_look(p.settings, s)) {
            return &p;
        }
    }
    return nullptr;
}

}  // namespace prism::gui
