// The app's HDR setting (#105): EditSettings' headroom and switch, the Output encode row's
// controls, recipes, undo, and the export that writes an Ultra HDR JPEG through the same code as
// the command line (prism::save_output).

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <stop_token>
#include <string>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "prism/gui/file_types.hpp"
#include "prism/gui/light_path.hpp"
#include "prism/gui/presets.hpp"
#include "prism/gui/recipe_settings.hpp"
#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"
#include "prism/gui/stage_controls.hpp"
#include "prism/image.hpp"
#include "prism/ranges.hpp"
#include "prism/recipe.hpp"
#include "prism/ultra_hdr.hpp"
#include "prism/utf8_path.hpp"

namespace fs = std::filesystem;

using prism::gui::EditSettings;
using prism::gui::ErrorKind;
using prism::gui::Session;
using prism::gui::StageId;

namespace {

fs::path temp_file(const std::string& name) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    return fs::path(testing::TempDir()) /
           ("prism_hdr_" + std::string(test->name()) + "_" + std::to_string(pid) + "_" + name);
}

// Removes a file when the test ends, however it ends.
struct RemoveOnExit {
    fs::path path;
    ~RemoveOnExit() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

std::vector<char> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    in.close();
    return bytes;
}

// A 41 x 29 PNG whose light rises to white at the right, so +1 stop takes part of it above white.
fs::path write_ramp_png(const std::string& name) {
    prism::Image img;
    img.width = 41;
    img.height = 29;
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const float t = static_cast<float>(x) / static_cast<float>(img.width - 1);
            img.pixels.push_back(t);
            img.pixels.push_back(t * (0.5f + 0.5f * static_cast<float>(y) / (img.height - 1)));
            img.pixels.push_back(0.18f);
        }
    }
    const fs::path path = temp_file(name);
    prism::save_image(path, img);
    return path;
}

EditSettings hdr_look(float headroom) {
    EditSettings s;
    s.exposure_stops = 1.0f;
    s.curve = prism::gui::CurveKind::filmic;
    s.hdr_on = true;
    s.headroom_stops = headroom;
    return s;
}

std::vector<std::uint32_t> bits(const prism::Image& img) {
    std::vector<std::uint32_t> out(img.pixels.size());
    std::memcpy(out.data(), img.pixels.data(), out.size() * sizeof(float));
    return out;
}

prism::gui::LightPathRow encode_row(const EditSettings& s, StageId stage = StageId::encode) {
    return prism::gui::light_path_row(
        {s, prism::gui::InputKind::raw, "RAW", prism::gui::final_stage, std::nullopt, nullptr,
         std::nullopt},
        stage);
}

std::string saved(const EditSettings& s) {
    return prism::write_recipe(prism::gui::recipe_from(s), prism::RecipeWriteOptions{false});
}

}  // namespace

// --- EditSettings --------------------------------------------------------------------------------

TEST(HdrSettings, OffByDefaultWaitingAtPlusTwoStops) {
    const EditSettings s;
    EXPECT_FALSE(s.hdr_on);
    EXPECT_EQ(s.headroom_stops, 2.0f);  // #96: default +2.0
    EXPECT_EQ(prism::gui::output_headroom(s), std::nullopt);
    EXPECT_TRUE(prism::gui::validate(s).empty());
    EXPECT_EQ(prism::gui::to_process_settings(s).headroom_stops, std::nullopt);
}

TEST(HdrSettings, OutputHeadroomIsTheHeadroomOnlyWhileOnAndAboveZero) {
    EditSettings s;
    s.headroom_stops = 3.0f;
    EXPECT_EQ(prism::gui::output_headroom(s), std::nullopt);
    s.hdr_on = true;
    EXPECT_EQ(prism::gui::output_headroom(s), 3.0f);
    s.headroom_stops = 0.1f;
    EXPECT_EQ(prism::gui::output_headroom(s), 0.1f);
    for (const float none : {0.0f, -0.0f, std::numeric_limits<float>::quiet_NaN()}) {
        s.headroom_stops = none;
        EXPECT_EQ(prism::gui::output_headroom(s), std::nullopt) << none;
    }
}

TEST(HdrSettings, ValidateRefusesAHeadroomOutsideZeroToFour) {
    for (const float bad : {4.01f, 5.0f, -0.1f, std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity()}) {
        for (const bool on : {true, false}) {  // checked while off too, as every stage is
            EditSettings s;
            s.hdr_on = on;
            s.headroom_stops = bad;
            const std::vector<std::string> problems = prism::gui::validate(s);
            ASSERT_EQ(problems.size(), 1u) << bad;
            EXPECT_EQ(problems[0], prism::range_message("Headroom", bad, prism::ValueRange{0, 4}));
            EXPECT_THROW((void)prism::gui::to_process_settings(s), std::invalid_argument);
        }
    }
    for (const float good : {0.0f, 4.0f, 2.5f}) {
        EditSettings s;
        s.headroom_stops = good;
        s.hdr_on = true;
        EXPECT_TRUE(prism::gui::validate(s).empty()) << good;
    }
}

TEST(HdrSettings, ProcessSettingsCarryTheHeadroomOnlyThroughOutputEncode) {
    const EditSettings s = hdr_look(3.0f);
    EXPECT_EQ(prism::gui::to_process_settings(s).headroom_stops, 3.0f);
    EXPECT_EQ(prism::gui::to_process_settings(s, StageId::encode).headroom_stops, 3.0f);
    for (const StageId earlier : {StageId::decode, StageId::contrast, StageId::curve}) {
        EXPECT_EQ(prism::gui::to_process_settings(s, earlier).headroom_stops, std::nullopt);
    }
    EditSettings off = s;
    off.hdr_on = false;
    EXPECT_EQ(prism::gui::to_process_settings(off).headroom_stops, std::nullopt);
    // Everything else is what it was without HDR.
    const prism::ProcessSettings with = prism::gui::to_process_settings(s);
    const prism::ProcessSettings without = prism::gui::to_process_settings(off);
    EXPECT_EQ(with.exposure_stops, without.exposure_stops);
    EXPECT_EQ(with.curve.has_value(), without.curve.has_value());
}

// --- The Output encode row -----------------------------------------------------------------------

TEST(HdrControls, TheHeadroomSnapsToTenthsFromZeroToFour) {
    using prism::gui::snap_headroom;
    EXPECT_EQ(snap_headroom(2.0f), 2.0f);
    EXPECT_FLOAT_EQ(snap_headroom(2.33f), 2.3f);
    EXPECT_FLOAT_EQ(snap_headroom(2.37f), 2.4f);
    EXPECT_EQ(snap_headroom(0.04f), 0.0f);
    EXPECT_EQ(snap_headroom(-1.0f), 0.0f);
    EXPECT_EQ(snap_headroom(4.0f), 4.0f);
    EXPECT_EQ(snap_headroom(9.0f), 4.0f);
    EXPECT_EQ(snap_headroom(std::numeric_limits<float>::infinity()), 4.0f);
    EXPECT_EQ(snap_headroom(std::numeric_limits<float>::quiet_NaN()), 2.0f);
}

TEST(HdrControls, MovingTheSliderTurnsHdrOnAndTheSwitchKeepsTheHeadroom) {
    EditSettings s;
    prism::gui::set_headroom(s, 3.26f);
    EXPECT_TRUE(s.hdr_on);
    EXPECT_FLOAT_EQ(s.headroom_stops, 3.3f);
    prism::gui::set_hdr_on(s, false);
    EXPECT_FALSE(s.hdr_on);
    EXPECT_FLOAT_EQ(s.headroom_stops, 3.3f);
    prism::gui::set_hdr_on(s, true);
    EXPECT_FLOAT_EQ(*prism::gui::output_headroom(s), 3.3f);
    prism::gui::set_headroom(s, 0.0f);
    EXPECT_TRUE(s.hdr_on);
    EXPECT_EQ(prism::gui::output_headroom(s), std::nullopt);  // on at 0: no HDR picture
}

TEST(HdrControls, TheRowShowsTheHeadroomInSignedStops) {
    EXPECT_EQ(prism::gui::headroom_text(2.0f), "+2.00 stops");
    EXPECT_EQ(prism::gui::headroom_text(0.0f), "+0.00 stops");
    EXPECT_EQ(prism::gui::headroom_text(4.0f), "+4.00 stops");
    EXPECT_EQ(prism::gui::headroom_text(1.26f), "+1.26 stops");  // as stored, not snapped
    EXPECT_EQ(prism::gui::headroom_text(9.0f), "+4.00 stops");
    EXPECT_EQ(prism::gui::headroom_text(4.0f), prism::gui::light_path_text::widest_headroom);

    for (const bool on : {false, true}) {
        EditSettings s;
        s.hdr_on = on;
        s.headroom_stops = 3.5f;
        const prism::gui::LightPathRow row = encode_row(s);
        ASSERT_TRUE(row.hdr.has_value());
        EXPECT_EQ(row.hdr->on, on);
        EXPECT_EQ(row.hdr->headroom, 3.5f);
        EXPECT_EQ(row.hdr->headroom_text, "+3.50 stops");
        EXPECT_EQ(row.hdr->headroom_reset, "Reset to +2.00 stops");
        EXPECT_FALSE(row.can_toggle);  // HDR's switch is its own, not the row's
        EXPECT_EQ(row.subtitle, "sRGB for the screen");
    }
    // No other row has it.
    const EditSettings s;
    for (const prism::gui::StageInfo& stage : prism::gui::pipeline_stages()) {
        if (stage.id != StageId::encode) {
            EXPECT_FALSE(encode_row(s, stage.id).hdr.has_value()) << stage.name;
        }
    }
}

TEST(HdrControls, TheControlsLineSitsUnderTheSubtitleAndTheRowNeverChangesHeight) {
    const prism::gui::LightPathMetrics m{.pad_top = 11.0f,
                                         .pad_bottom = 9.0f,
                                         .line = 20.0f,
                                         .secondary = 15.0f,
                                         .slider = 22.0f,
                                         .tabs = 26.0f,
                                         .gap = 4.0f};
    const EditSettings off;
    const prism::gui::LightPathRowLayout layout =
        prism::gui::light_path_row_layout(encode_row(off), m);
    ASSERT_TRUE(layout.subtitle_y && layout.controls_y);
    EXPECT_EQ(*layout.subtitle_y, m.pad_top + m.line + m.gap);
    EXPECT_EQ(*layout.controls_y, *layout.subtitle_y + m.secondary + m.gap);
    EXPECT_EQ(layout.clip_y, *layout.controls_y + m.slider + m.gap);
    EXPECT_FALSE(layout.slider_y.has_value());
    for (const float headroom : {0.0f, 2.0f, 4.0f}) {
        const EditSettings on = hdr_look(headroom);
        EXPECT_EQ(prism::gui::light_path_row_layout(encode_row(on), m), layout);
    }
}

// --- Recipes -------------------------------------------------------------------------------------

TEST(HdrRecipe, AFormat3RecipeOpenedAndSavedAgainKeepsItsHdr) {
    // The app used to drop [hdr] (#105's comment): EditSettings had no headroom.
    const std::string text = "prism_recipe = 3\n[exposure]\nstops = +1.0\n[hdr]\nheadroom = +2.5\n";
    EditSettings s;
    prism::gui::apply_recipe(s, prism::parse_recipe(text));
    EXPECT_TRUE(s.hdr_on);
    EXPECT_EQ(s.headroom_stops, 2.5f);
    const prism::Recipe back = prism::gui::recipe_from(s);
    ASSERT_TRUE(back.hdr.has_value());
    EXPECT_EQ(back.hdr->headroom, 2.5f);
    const std::string written = prism::write_recipe(back, prism::RecipeWriteOptions{false});
    EXPECT_EQ(written.rfind("prism_recipe = 3", 0), 0u) << written;
    EXPECT_NE(written.find("[hdr]\nheadroom = +2.5\n"), std::string::npos) << written;

    // Through a Session as the app opens it, and as one undo step.
    Session session;
    ASSERT_EQ(session.apply_recipe(prism::parse_recipe(text)), std::nullopt);
    EXPECT_EQ(prism::gui::output_headroom(session.settings()), 2.5f);
    ASSERT_EQ(session.undo(), std::nullopt);
    EXPECT_EQ(session.settings(), EditSettings{});
}

TEST(HdrRecipe, ZeroTurnsHdrOffAndARecipeWithoutHdrLeavesItAlone) {
    EditSettings s = hdr_look(3.0f);
    prism::Recipe zero;
    zero.hdr = prism::RecipeHdr{0.0f};
    prism::gui::apply_recipe(s, zero);
    EXPECT_FALSE(s.hdr_on);
    EXPECT_EQ(s.headroom_stops, 3.0f);  // what the switch comes back with

    EditSettings on = hdr_look(3.0f);
    prism::Recipe none;
    none.exposure = prism::RecipeExposure{0.5f, true};
    prism::gui::apply_recipe(on, none);
    EXPECT_TRUE(on.hdr_on);
    EXPECT_EQ(on.headroom_stops, 3.0f);
}

TEST(HdrRecipe, OffOrZeroWritesNoSectionSoTheOlderFormatStays) {
    EditSettings s;
    s.headroom_stops = 3.0f;
    EXPECT_FALSE(prism::gui::recipe_from(s).hdr.has_value());
    EXPECT_EQ(saved(s).rfind("prism_recipe = 1", 0), 0u);
    s.hdr_on = true;
    s.headroom_stops = 0.0f;
    EXPECT_FALSE(prism::gui::recipe_from(s).hdr.has_value());
    EXPECT_EQ(saved(s), saved(EditSettings{}));
}

TEST(HdrRecipe, TheModifiedCheckFollowsTheHeadroomOnlyWhileHdrIsOn) {
    EditSettings a;
    EditSettings b;
    b.headroom_stops = 3.0f;
    EXPECT_TRUE(prism::gui::same_recipe(a, b));  // both off: no [hdr] in either
    a.hdr_on = true;
    EXPECT_FALSE(prism::gui::same_recipe(a, b));
    b.hdr_on = true;
    EXPECT_FALSE(prism::gui::same_recipe(a, b));  // +2.0 against +3.0
    b.headroom_stops = 2.0f;
    EXPECT_TRUE(prism::gui::same_recipe(a, b));
}

TEST(HdrRecipe, TheBuiltInLooksHaveNoHdrAndLeaveItAsItIs) {
    for (const prism::gui::InputKind kind :
         {prism::gui::InputKind::raw, prism::gui::InputKind::image}) {
        for (const prism::gui::Preset& look : prism::gui::presets_for(kind)) {
            EXPECT_FALSE(look.settings.hdr_on) << look.name;
            EXPECT_EQ(look.settings.headroom_stops, 2.0f) << look.name;
            EXPECT_FALSE(prism::gui::recipe_from(look.settings).hdr.has_value()) << look.name;
        }
    }
}

// --- Undo ----------------------------------------------------------------------------------------

TEST(HdrUndo, TheSwitchAndTheHeadroomAreUndoSteps) {
    const RemoveOnExit source{write_ramp_png("in.png")};
    Session session;
    ASSERT_EQ(session.open_file(source.path), std::nullopt);
    const EditSettings start = session.settings();
    EditSettings s = start;
    prism::gui::set_hdr_on(s, true);
    ASSERT_EQ(session.set_settings(s), std::nullopt);
    prism::gui::set_headroom(s, 3.0f);
    ASSERT_EQ(session.set_settings(s), std::nullopt);
    EXPECT_EQ(prism::gui::output_headroom(session.settings()), 3.0f);
    ASSERT_EQ(session.undo(), std::nullopt);
    EXPECT_EQ(prism::gui::output_headroom(session.settings()), 2.0f);
    ASSERT_EQ(session.undo(), std::nullopt);
    EXPECT_EQ(session.settings(), start);
    ASSERT_EQ(session.redo(), std::nullopt);
    EXPECT_TRUE(session.settings().hdr_on);
}

// --- Export --------------------------------------------------------------------------------------

TEST(HdrExport, TheSaveDialogOffersJpegWhileHdrIsOn) {
    using prism::gui::ExportFormat;
    EXPECT_EQ(prism::gui::default_export_format("a.png"), ExportFormat::png);
    EXPECT_EQ(prism::gui::default_export_format("a.png", false), ExportFormat::png);
    EXPECT_EQ(prism::gui::default_export_format("a.PNG", true), ExportFormat::jpeg);
    EXPECT_EQ(prism::gui::default_export_format("a.ARW", true), ExportFormat::jpeg);
    EXPECT_EQ(prism::gui::default_export_format("a.jpg", false), ExportFormat::jpeg);
}

TEST(HdrExport, AJpgIsTheUltraHdrJpegOfTheSameStagesAtTheHeadroom) {
    const RemoveOnExit source{write_ramp_png("in.png")};
    Session session;
    ASSERT_EQ(session.open_file(source.path), std::nullopt);
    ASSERT_EQ(session.set_settings(hdr_look(1.5f)), std::nullopt);
    prism::gui::ExportJob job = session.export_job();
    job.threads = 1;

    // What the export should be: export's own decode and stages with the HDR picture, then the
    // library's Ultra HDR JPEG of the two at +1.5 stops.
    std::variant<prism::Photo, prism::gui::Error> decoded =
        prism::gui::decode_for_export(job.source, job.kind, job.settings, 1);
    ASSERT_TRUE(std::holds_alternative<prism::Photo>(decoded));
    prism::Photo again = std::get<prism::Photo>(decoded);
    prism::Image hdr;
    std::variant<prism::Image, prism::gui::Error> sdr = prism::gui::finish_export(
        std::move(std::get<prism::Photo>(decoded)), job.settings, std::nullopt, 1, {}, {}, &hdr);
    ASSERT_TRUE(std::holds_alternative<prism::Image>(sdr));
    ASSERT_FALSE(hdr.pixels.empty());
    // The SDR picture is exactly what it is without HDR.
    std::variant<prism::Image, prism::gui::Error> plain =
        prism::gui::finish_export(std::move(again), job.settings, std::nullopt, 1);
    ASSERT_TRUE(std::holds_alternative<prism::Image>(plain));
    EXPECT_EQ(bits(std::get<prism::Image>(sdr)), bits(std::get<prism::Image>(plain)));
    const prism::UltraHdrJpeg want = prism::encode_ultra_hdr_jpeg(
        prism::encode_srgb8(std::get<prism::Image>(sdr), 1), hdr, 1.5f, {}, 1);

    for (const char* name : {"out.jpg", "out.JPEG"}) {
        const RemoveOnExit out{temp_file(name)};
        ASSERT_EQ(prism::gui::export_image(job, out.path), std::nullopt) << name;
        const std::vector<char> got = read_file(out.path);
        EXPECT_TRUE(got == std::vector<char>(want.bytes.begin(), want.bytes.end()))
            << name << ": " << got.size() << " bytes, want " << want.bytes.size();
    }
}

TEST(HdrExport, APngIsRefusedBeforeAnythingIsDecoded) {
    const fs::path source = write_ramp_png("in.png");
    Session session;
    ASSERT_EQ(session.open_file(source), std::nullopt);
    ASSERT_EQ(session.set_settings(hdr_look(2.0f)), std::nullopt);
    const prism::gui::ExportJob job = session.export_job();
    fs::remove(source);  // a decode would fail with another message
    const RemoveOnExit out{temp_file("out.png")};
    const prism::gui::MaybeError error = prism::gui::export_image(job, out.path);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->kind, ErrorKind::unsupported_format);
    EXPECT_EQ(error->message, "HDR files need .jpg: give '" + prism::utf8(out.path.filename()) +
                                  "' the extension .jpg or .jpeg, or turn HDR off to save a PNG.");
    EXPECT_FALSE(fs::exists(out.path));
}

TEST(HdrExport, OffOrAtZeroTheFilesAreTheSdrOnesAsBefore) {
    const RemoveOnExit source{write_ramp_png("in.png")};
    Session session;
    ASSERT_EQ(session.open_file(source.path), std::nullopt);
    EditSettings off = hdr_look(2.0f);
    off.hdr_on = false;
    EditSettings zero = hdr_look(0.0f);
    for (const char* name : {"a.png", "a.jpg"}) {
        ASSERT_EQ(session.set_settings(off), std::nullopt);
        const RemoveOnExit want{temp_file(std::string("off_") + name)};
        ASSERT_EQ(prism::gui::export_image(session.export_job(), want.path), std::nullopt);
        ASSERT_EQ(session.set_settings(zero), std::nullopt);
        const RemoveOnExit got{temp_file(std::string("zero_") + name)};
        ASSERT_EQ(prism::gui::export_image(session.export_job(), got.path), std::nullopt);
        EXPECT_EQ(read_file(got.path), read_file(want.path)) << name;
        // And exactly save_image of the stages' picture.
        prism::Image img = prism::load_image(source.path);
        (void)prism::process(img, prism::gui::to_process_settings(off), 0);
        const RemoveOnExit direct{temp_file(std::string("direct_") + name)};
        prism::save_image(direct.path, img);
        EXPECT_EQ(read_file(want.path), read_file(direct.path)) << name;
    }
}

TEST(HdrControls, AnOffStepHeadroomFromARecipeIsShownAndExportedAsItIs) {
    EditSettings s;
    prism::gui::apply_recipe(s, prism::parse_recipe("prism_recipe = 3\n[hdr]\nheadroom = +2.25\n"));
    const prism::gui::LightPathRow row = encode_row(s);
    ASSERT_TRUE(row.hdr.has_value());
    EXPECT_EQ(row.hdr->headroom, 2.25f);
    EXPECT_EQ(row.hdr->headroom_text, "+2.25 stops");
    EXPECT_EQ(prism::gui::output_headroom(s), 2.25f);  // what export and the recipe use
    EXPECT_EQ(prism::gui::recipe_from(s).hdr->headroom, 2.25f);
}

TEST(HdrExport, FinishExportEmptiesTheHdrPictureWithoutAHeadroom) {
    const RemoveOnExit source{write_ramp_png("in.png")};
    EditSettings off = hdr_look(2.0f);
    off.hdr_on = false;
    std::variant<prism::Photo, prism::gui::Error> decoded =
        prism::gui::decode_for_export(source.path, prism::gui::InputKind::image, off, 1);
    ASSERT_TRUE(std::holds_alternative<prism::Photo>(decoded));
    prism::Image hdr;
    hdr.width = 1;
    hdr.height = 1;
    hdr.pixels = {1.0f, 2.0f, 3.0f};
    const std::variant<prism::Image, prism::gui::Error> sdr = prism::gui::finish_export(
        std::move(std::get<prism::Photo>(decoded)), off, std::nullopt, 1, {}, {}, &hdr);
    ASSERT_TRUE(std::holds_alternative<prism::Image>(sdr));
    EXPECT_TRUE(hdr.pixels.empty());
    EXPECT_EQ(hdr.width, 0);
}

TEST(HdrExport, AStoppedHdrExportWritesNothing) {
    const RemoveOnExit source{write_ramp_png("in.png")};
    Session session;
    ASSERT_EQ(session.open_file(source.path), std::nullopt);
    ASSERT_EQ(session.set_settings(hdr_look(2.0f)), std::nullopt);
    for (const char* at : {"exposure", "hdr_curve"}) {
        std::stop_source stopper;
        bool hdr_curve_ran = false;
        const RemoveOnExit out{temp_file(std::string("stopped_") + at + ".jpg")};
        const prism::gui::MaybeError error = prism::gui::export_image(
            session.export_job(), out.path, stopper.get_token(), [&](const prism::StageTime& t) {
                hdr_curve_ran = hdr_curve_ran || t.name == "hdr_curve";
                if (t.name == at) {
                    stopper.request_stop();
                }
            });
        ASSERT_TRUE(error.has_value()) << at;
        EXPECT_EQ(error->kind, ErrorKind::cancelled) << at;
        EXPECT_FALSE(fs::exists(out.path)) << at;
        EXPECT_EQ(hdr_curve_ran, std::string(at) == "hdr_curve") << at;
    }
}
