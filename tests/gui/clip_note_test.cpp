#include "prism/gui/clip_note.hpp"

#include <gtest/gtest.h>

#include "prism/gui/clipping.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"

namespace prism::gui {
namespace {

Image solid(int w, int h, float r, float g, float b) {
    Image img;
    img.width = w;
    img.height = h;
    for (int i = 0; i < w * h; ++i) {
        img.pixels.insert(img.pixels.end(), {r, g, b});
    }
    return img;
}

TEST(ClipNote, NothingClippingIsSaidPlainly) {
    const ClipNote empty = make_clip_note(ClipReport{});
    EXPECT_EQ(empty.kind, ClipNoteKind::none);
    EXPECT_EQ(empty.text, "No clipping");
    EXPECT_EQ(empty.full, "Nothing clips.");

    const ClipNote fine = make_clip_note(compute_clip_report(solid(10, 10, 0.3f, 0.3f, 0.3f), EditSettings{}));
    EXPECT_EQ(fine.kind, ClipNoteKind::none);
    EXPECT_EQ(fine.text, "No clipping");
}

TEST(ClipNote, PixelsAtTheFilesCeilingAreClippedInTheFile) {
    Image img = solid(10, 10, 0.3f, 0.3f, 0.3f);
    for (int i = 0; i < 10; ++i) {  // 10 of 100 pixels at the ceiling
        img.pixels[static_cast<std::size_t>(i) * 3] = 1.0f;
    }
    const ClipReport report = compute_clip_report(img, EditSettings{});
    const ClipNote note = make_clip_note(report);
    EXPECT_EQ(note.kind, ClipNoteKind::highlights);
    EXPECT_EQ(note.text, "10% clipped in the file");
    EXPECT_EQ(note.full, describe_clipping(report));
}

TEST(ClipNote, SmallSharesReadAsLessThanATenthOfAPercent) {
    Image img = solid(100, 100, 0.3f, 0.3f, 0.3f);
    img.pixels[0] = 1.0f;  // 1 of 10000
    const ClipNote note = make_clip_note(compute_clip_report(img, EditSettings{}));
    EXPECT_EQ(note.text, "<0.1% clipped in the file");
}

TEST(ClipNote, AnEditThatPushesPastWhiteNamesTheStageThatCutsItOff) {
    EditSettings s;
    s.exposure_stops = 2.0f;
    const ClipReport report = compute_clip_report(solid(10, 10, 0.5f, 0.5f, 0.5f), s);
    const ClipNote note = make_clip_note(report);
    EXPECT_EQ(note.kind, ClipNoteKind::highlights);
    EXPECT_EQ(note.text, "100% highlights clipped at Output encode");
    EXPECT_EQ(note.full, describe_clipping(report));
}

TEST(ClipNote, AToneCurveThatBringsItBackMeansNoClipping) {
    EditSettings s;
    s.exposure_stops = 1.0f;
    s.curve = CurveKind::filmic;
    // 0.4 * 2 = 0.8, then the curve keeps it under white.
    EXPECT_EQ(make_clip_note(compute_clip_report(solid(10, 10, 0.4f, 0.4f, 0.4f), s)).kind,
              ClipNoteKind::none);
}

TEST(ClipNote, ShadowsAndColoursOutsideSrgbHaveTheirOwnKinds) {
    EditSettings s;
    s.matrix = MatrixFile{{}, {1, -20, 0, 0, 1, 0, 0, 0, 1}};
    s.saturation = 1.5f;
    const ClipReport report = compute_clip_report(solid(10, 10, 0.1f, 0.5f, 0.1f), s);
    const ClipNote note = make_clip_note(report);
    EXPECT_EQ(note.kind, ClipNoteKind::shadows);
    EXPECT_EQ(note.text, "100% shadows clipped at Saturation");

    // Colours that still have light but a channel below zero: outside sRGB.
    EditSettings g;
    g.matrix = MatrixFile{{}, {1, -2, 0, 0, 1, 0, 0, 0, 1}};
    g.saturation = 1.5f;
    const ClipReport colours = compute_clip_report(solid(10, 10, 0.2f, 0.3f, 0.5f), g);
    ASSERT_TRUE(colours.first_gamut);
    const ClipNote gamut = make_clip_note(colours);
    EXPECT_EQ(gamut.kind, ClipNoteKind::gamut);
    EXPECT_NE(gamut.text.find("too saturated, clipped at "), std::string::npos) << gamut.text;
    EXPECT_NE(gamut.full.find("too saturated for sRGB"), std::string::npos) << gamut.full;
}

TEST(ClipNote, SeveralKindsShowTheMostImportantAndCountTheRest) {
    // One pixel at the ceiling, one with no light.
    Image img;
    img.width = 2;
    img.height = 1;
    img.pixels = {1.0f, 0.5f, 0.5f, -1.0f, 0.2f, 0.2f};
    EditSettings s;
    const ClipNote note = make_clip_note(compute_clip_report(img, s));
    EXPECT_EQ(note.kind, ClipNoteKind::highlights);
    EXPECT_EQ(note.text, "50% clipped in the file  +1 more");
}

TEST(ClipNote, AFileClippedAndPushedFurtherShowsTheFileFirstAndTheEditsAfter) {
    Image img = solid(10, 10, 0.5f, 0.5f, 0.5f);
    for (int i = 0; i < 10; ++i) {
        img.pixels[static_cast<std::size_t>(i) * 3] = 1.0f;
    }
    EditSettings s;
    s.exposure_stops = 2.0f;
    const ClipNote note = make_clip_note(compute_clip_report(img, s));
    EXPECT_EQ(note.text, "10% clipped in the file  +1 more");
}

TEST(ClipNote, TheNoteAgreesWithThePipelineViewsFilledDots) {
    // Whatever the note names must be a stage the report marks as having lost something.
    EditSettings s;
    s.exposure_stops = 3.0f;
    s.curve = CurveKind::gamma;
    const ClipReport report = compute_clip_report(solid(8, 8, 0.4f, 0.2f, 0.9f), s);
    const ClipNote note = make_clip_note(report);
    ASSERT_EQ(note.kind, ClipNoteKind::highlights);
    ASSERT_TRUE(report.first_over_lost);
    EXPECT_NE(note.text.find(std::string(stage_info(*report.first_over_lost).name)),
              std::string::npos)
        << note.text;
}

}  // namespace
}  // namespace prism::gui
