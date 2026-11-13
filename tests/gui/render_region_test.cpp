#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "env_var.hpp"
#include "prism/frame.hpp"
#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/parallel.hpp"
#include "prism/raw.hpp"
#include "prism/stroke_field.hpp"
#include "prism/utf8_path.hpp"

// decode_for_export, finish_export and render_region (#37): a part of the finished, framed picture
// made from a kept full-size decode is bit-identical to the same pixels of what export makes
// before encoding it.

namespace fs = std::filesystem;

namespace {

using prism::Aspect;
using prism::FieldStroke;
using prism::Frame;
using prism::FrameBox;
using prism::Image;
using prism::Photo;
using prism::PixelSize;
using prism::StrokeField;
using prism::gui::EditSettings;
using prism::gui::Error;
using prism::gui::ErrorKind;
using prism::gui::InputKind;
using prism::gui::Strokes;

// A w x h photo of varied colours, different in every pixel's neighbourhood, so a misplaced
// stroke or a wrong tap changes many values. Its as-shot light lets White balance run.
Photo make_photo(int w, int h) {
    Photo photo;
    photo.image.width = w;
    photo.image.height = h;
    photo.image.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float* p = photo.image.pixels.data() +
                       (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                        static_cast<std::size_t>(x)) *
                           3;
            const float s = 0.03f + static_cast<float>((x * 7 + y * 13) % 101) / 120.0f;
            p[0] = s;
            p[1] = 0.6f * s + 0.02f * static_cast<float>(x % 5);
            p[2] = 0.2f + static_cast<float>(y % 11) / 30.0f;
        }
    }
    photo.info.as_shot_white_xy = std::array<double, 2>{0.3457, 0.3585};
    return photo;
}

// Strokes across a w x h photo: a long soft burn, a soft dodge dab, a hard dodge line straight
// across the middle and a hard burn down it, so the parts below have stroke edges running through
// them and across their sides.
Strokes make_strokes(int w, int h) {
    auto field = std::make_shared<StrokeField>();
    field->aspect = prism::aspect_of(w, h);
    field->strokes.push_back(std::make_shared<const FieldStroke>(
        FieldStroke{{{0.05f, 0.2f}, {0.5f, 0.55f}, {0.95f, 0.4f}}, 0.12f, 0.8f, -1.2f}));
    field->strokes.push_back(
        std::make_shared<const FieldStroke>(FieldStroke{{{0.45f, 0.6f}}, 0.2f, 1.0f, 1.5f}));
    field->strokes.push_back(std::make_shared<const FieldStroke>(
        FieldStroke{{{0.0f, 0.5f}, {1.0f, 0.52f}}, 0.04f, 0.0f, 0.7f}));
    field->strokes.push_back(std::make_shared<const FieldStroke>(
        FieldStroke{{{0.5f, 0.0f}, {0.48f, 1.0f}}, 0.03f, 0.0f, -0.6f}));
    return Strokes(std::move(field));
}

// Every stage on: exposure, strokes, white balance, saturation (0 = black and white), contrast
// and the filmic curve.
EditSettings look(const std::optional<Frame>& frame, int w, int h, bool black_and_white) {
    EditSettings s;
    s.exposure_stops = 0.4f;
    s.strokes = make_strokes(w, h);
    s.white_balance = prism::WhiteBalance{5200.0, 8.0};
    s.saturation = black_and_white ? 0.0f : 1.3f;
    s.contrast = 1.15f;
    s.curve = prism::gui::CurveKind::filmic;
    s.frame = frame;
    return s;
}

Frame turned(float angle) {
    Frame f;
    f.angle = angle;
    f.left = 0.04f;
    f.top = 0.06f;
    f.right = 0.97f;
    f.bottom = 0.95f;
    f.aspect = Aspect::free;
    return f;
}

Frame cropped_to_aspect() {
    Frame f;
    f.angle = -0.8f;
    f.left = 0.05f;
    f.top = 0.05f;
    f.right = 0.95f;
    f.bottom = 0.95f;
    f.aspect = Aspect::four_five;
    f.portrait = false;
    return f;
}

// export's picture before encoding, at the full framed size.
Image finished(Photo base, const EditSettings& s, int threads) {
    std::variant<Image, Error> out =
        prism::gui::finish_export(std::move(base), s, std::nullopt, threads);
    if (const Error* e = std::get_if<Error>(&out)) {
        ADD_FAILURE() << "finish_export failed: " << e->message;
        return {};
    }
    return std::move(std::get<Image>(out));
}

// The first float of `part` of `whole` whose bits differ from `got`'s, as a message; empty if
// every one is the same. No tolerance: bits, so -0 and NaN count too.
std::string first_difference(const Image& whole, const FrameBox& part, const Image& got) {
    if (got.width != part.width || got.height != part.height ||
        got.pixels.size() !=
            static_cast<std::size_t>(part.width) * static_cast<std::size_t>(part.height) * 3) {
        return "size " + std::to_string(got.width) + " x " + std::to_string(got.height) +
               ", wanted " + std::to_string(part.width) + " x " + std::to_string(part.height);
    }
    for (int y = 0; y < part.height; ++y) {
        for (int x = 0; x < part.width; ++x) {
            for (int c = 0; c < 3; ++c) {
                const float want = whole.pixels[(static_cast<std::size_t>(part.y0 + y) *
                                                     static_cast<std::size_t>(whole.width) +
                                                 static_cast<std::size_t>(part.x0 + x)) *
                                                    3 +
                                                static_cast<std::size_t>(c)];
                const float have = got.pixels[(static_cast<std::size_t>(y) *
                                                   static_cast<std::size_t>(part.width) +
                                               static_cast<std::size_t>(x)) *
                                                  3 +
                                              static_cast<std::size_t>(c)];
                if (std::bit_cast<std::uint32_t>(want) != std::bit_cast<std::uint32_t>(have)) {
                    return "first difference at part pixel (" + std::to_string(x) + ", " +
                           std::to_string(y) + ") channel " + std::to_string(c) + ": got " +
                           std::to_string(have) + ", export has " + std::to_string(want);
                }
            }
        }
    }
    return {};
}

std::string describe(const FrameBox& b) {
    return "(" + std::to_string(b.x0) + ", " + std::to_string(b.y0) + ") " +
           std::to_string(b.width) + " x " + std::to_string(b.height);
}

// The parts every frame is checked at, on a W x H framed picture: the centre, a strip along each
// edge, each corner, one pixel, an odd part straddling the stages' 64-row tiles, and the whole.
std::vector<FrameBox> parts_of(PixelSize size) {
    const int w = size.width;
    const int h = size.height;
    return {
        {w / 4, h / 4, w / 2, h / 2},  // centre
        {w / 3, 0, w / 3, 5},          // top edge
        {w / 3, h - 5, w / 3, 5},      // bottom edge
        {0, h / 3, 6, h / 3},          // left edge
        {w - 6, h / 3, 6, h / 3},      // right edge
        {0, 0, 9, 7},                  // corners
        {w - 9, 0, 9, 7},
        {0, h - 7, 9, 7},
        {w - 9, h - 7, 9, 7},
        {w / 2 + 1, h / 3, 1, 1},  // one pixel
        {w / 5, 40, w / 2 + 3, std::min(91, h - 40)},  // across the row-64 tile line
        {0, 0, w, h},                // the whole
    };
}

struct Case {
    const char* name;
    std::optional<Frame> frame;
};

std::vector<Case> frames() {
    Frame upright = turned(0.0f);
    return {{"no frame", std::nullopt},
            {"0 degrees, cropped", upright},
            {"7 degrees", turned(7.0f)},
            {"-45 degrees", turned(-45.0f)},
            {"crop and 4:5", cropped_to_aspect()}};
}

// --- Bit-identity with export ---------------------------------------------------------------------

TEST(RenderRegion, EveryPartMatchesExportBitForBit) {
    const int w = 401;
    const int h = 303;
    const Photo base = make_photo(w, h);
    for (const Case& c : frames()) {
        for (const bool bw : {false, true}) {
            SCOPED_TRACE(std::string(c.name) + (bw ? ", black and white" : ", colour"));
            const EditSettings s = look(c.frame, w, h, bw);
            const Image whole = finished(base, s, 1);
            const PixelSize size = prism::plan_export_frame(c.frame, w, h).size;
            ASSERT_EQ(whole.width, size.width);
            ASSERT_EQ(whole.height, size.height);
            if (c.frame) {
                ASSERT_TRUE(prism::plan_export_frame(c.frame, w, h).fitted);
                ASSERT_LT(size.width, w);  // the frame really frames
            }
            for (const FrameBox& part : parts_of(size)) {
                SCOPED_TRACE(describe(part));
                const Image got = prism::gui::render_region(base, s, part, 1);
                EXPECT_EQ(first_difference(whole, part, got), "");
            }
        }
    }
}

// The strokes really act on the parts above, and across their edges: without them the centre
// part differs, and the hard line's edge lies inside it (some pixels of a row change and some
// don't). Otherwise the test above would not see a misplaced stroke.
TEST(RenderRegion, TheStrokesReachThePartsTested) {
    const int w = 401;
    const int h = 303;
    const Photo base = make_photo(w, h);
    for (const Case& c : frames()) {
        SCOPED_TRACE(c.name);
        const EditSettings with = look(c.frame, w, h, false);
        EditSettings without = with;
        without.strokes = {};
        const PixelSize size = prism::plan_export_frame(c.frame, w, h).size;
        for (const FrameBox& part : parts_of(size)) {
            if (part.width * part.height < 1000) {
                continue;  // the strips and corners may lie outside every stroke
            }
            SCOPED_TRACE(describe(part));
            const Image a = prism::gui::render_region(base, with, part, 1);
            const Image b = prism::gui::render_region(base, without, part, 1);
            std::size_t changed = 0;
            for (std::size_t i = 0; i < a.pixels.size(); ++i) {
                changed += a.pixels[i] != b.pixels[i] ? 1u : 0u;
            }
            EXPECT_GT(changed, 0u);
            EXPECT_LT(changed, a.pixels.size());
        }
    }
}

// --- Threads ---------------------------------------------------------------------------------------

TEST(RenderRegion, OneAndEightThreadsGiveTheSamePart) {
    const int w = 2000;
    const int h = 1400;
    const Photo base = make_photo(w, h);
    const EditSettings s = look(turned(7.0f), w, h, false);
    const PixelSize size = prism::plan_export_frame(s.frame, w, h).size;
    const FrameBox part{(size.width - 1501) / 2, (size.height - 1049) / 2, 1501, 1049};
    const std::size_t pixels =
        static_cast<std::size_t>(part.width) * static_cast<std::size_t>(part.height);
    ASSERT_GE(part.x0, 0);
    ASSERT_GE(part.y0, 0);
    ASSERT_GE(pixels, prism::parallel_min_pixels * 3 / 2);
    ASSERT_EQ(prism::threads_for_pixels(pixels, 8), 8);

    const Image one = prism::gui::render_region(base, s, part, 1);
    const Image eight = prism::gui::render_region(base, s, part, 8);
    ASSERT_EQ(one.width, part.width);
    ASSERT_EQ(one.height, part.height);
    EXPECT_EQ(first_difference(one, {0, 0, part.width, part.height}, eight), "");
}

// --- Refused parts ---------------------------------------------------------------------------------

TEST(RenderRegion, AnEmptyPartOrOnePastThePictureIsRefused) {
    const int w = 120;
    const int h = 90;
    const Photo base = make_photo(w, h);
    for (const std::optional<Frame>& frame : {std::optional<Frame>{}, std::optional(turned(7.0f))}) {
        SCOPED_TRACE(frame ? "framed" : "no frame");
        const EditSettings s = look(frame, w, h, false);
        const PixelSize size = prism::plan_export_frame(frame, w, h).size;
        const std::vector<FrameBox> refused = {
            {0, 0, 0, 0},
            {3, 4, 0, 5},
            {3, 4, 5, 0},
            {-1, 0, 4, 4},
            {0, -1, 4, 4},
            {size.width - 3, 0, 4, 4},
            {0, size.height - 3, 4, 4},
            {size.width, size.height, 1, 1},
            {0, 0, size.width + 1, size.height},
        };
        for (const FrameBox& part : refused) {
            SCOPED_TRACE(describe(part));
            // render_region's own check, not a later step's: its message names the part.
            try {
                (void)prism::gui::render_region(base, s, part, 1);
                ADD_FAILURE() << "not refused";
            } catch (const std::invalid_argument& e) {
                EXPECT_NE(std::string(e.what()).find("is empty or not inside the " +
                                                     std::to_string(size.width) + " × " +
                                                     std::to_string(size.height) +
                                                     " framed picture"),
                          std::string::npos)
                    << e.what();
            }
        }
        // The largest part, the whole, is not refused.
        EXPECT_NO_THROW(
            (void)prism::gui::render_region(base, s, {0, 0, size.width, size.height}, 1));
    }
}

// --- The decode ------------------------------------------------------------------------------------

fs::path temp_file(const std::string& name) {
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    return fs::temp_directory_path() / ("prism_render_region_" + std::to_string(pid) + "_" + name);
}

void expect_same_error(const Error& a, const Error& b) {
    EXPECT_EQ(a.kind, b.kind);
    EXPECT_EQ(a.message, b.message);
    EXPECT_EQ(a.details, b.details);
}

// decode_for_export fails as export does, with export's own words: a missing file is "not found"
// (not a decoder failure), a damaged one "unreadable".
TEST(DecodeForExport, FailsAsExportDoes) {
    const fs::path out = temp_file("out.png");
    const EditSettings s;

    const fs::path missing = temp_file("missing.png");
    std::error_code ec;
    fs::remove(missing, ec);
    {
        const std::variant<Photo, Error> decoded =
            prism::gui::decode_for_export(missing, InputKind::image, s, 1);
        ASSERT_TRUE(std::holds_alternative<Error>(decoded));
        const Error& e = std::get<Error>(decoded);
        EXPECT_EQ(e.kind, ErrorKind::not_found);
        EXPECT_EQ(e.message, "Couldn't find '" + prism::utf8(missing.filename()) +
                                 "'. It may have been moved or deleted.");
        const std::optional<Error> exported =
            prism::gui::export_image({missing, InputKind::image, s, 1, std::nullopt}, out);
        ASSERT_TRUE(exported);
        expect_same_error(e, *exported);
    }

    const fs::path damaged = temp_file("damaged.png");
    {
        std::ofstream file(damaged, std::ios::binary);
        file << "not a png at all";
    }
    {
        const std::variant<Photo, Error> decoded =
            prism::gui::decode_for_export(damaged, InputKind::image, s, 1);
        ASSERT_TRUE(std::holds_alternative<Error>(decoded));
        const Error& e = std::get<Error>(decoded);
        EXPECT_EQ(e.kind, ErrorKind::unreadable);
        EXPECT_EQ(e.message, "Couldn't open '" + prism::utf8(damaged.filename()) +
                                 "'. The file may be damaged, or not really a PNG.");
        EXPECT_FALSE(e.details.empty());
        const std::optional<Error> exported =
            prism::gui::export_image({damaged, InputKind::image, s, 1, std::nullopt}, out);
        ASSERT_TRUE(exported);
        expect_same_error(e, *exported);
    }
    fs::remove(damaged, ec);
    EXPECT_FALSE(fs::exists(out));
}

// --- The real RAW file -----------------------------------------------------------------------------

// One decode of $PRISM_TEST_RAW (~700 MB as float): the parts are made from it first, then it is
// moved into finish_export for the whole picture they are compared with.
TEST(RenderRegion, PartsOfTheRawFileMatchExport) {
    const std::string raw = prism_test::env_var("PRISM_TEST_RAW");
    if (raw.empty()) {
        GTEST_SKIP() << "set PRISM_TEST_RAW to a RAW file to run this test";
    }
    if (!prism::raw_support_available()) {
        GTEST_SKIP() << "Prism was built without LibRaw";
    }
    const auto start = std::chrono::steady_clock::now();
    EditSettings s;
    std::variant<Photo, Error> decoded =
        prism::gui::decode_for_export(fs::path(raw), InputKind::raw, s, 0);
    if (const Error* e = std::get_if<Error>(&decoded)) {
        FAIL() << e->message << " " << e->details;
    }
    Photo& base = std::get<Photo>(decoded);
    const int w = base.image.width;
    const int h = base.image.height;
    s = look(turned(7.0f), w, h, false);
    const PixelSize size = prism::plan_export_frame(s.frame, w, h).size;
    const std::vector<FrameBox> parts = {
        {size.width / 2 - 450, size.height / 2 - 300, 900, 600},  // the centre, under the strokes
        {0, 0, 300, 200},                                          // the top-left corner
        {size.width - 257, size.height - 131, 257, 131},           // the bottom-right corner
        {size.width / 3, size.height / 2 - 40, 777, 80},           // across the hard line
    };
    std::vector<Image> got;
    for (const FrameBox& part : parts) {
        got.push_back(prism::gui::render_region(base, s, part, 0));
    }
    const Image whole = finished(std::move(base), s, 0);
    ASSERT_EQ(whole.width, size.width);
    ASSERT_EQ(whole.height, size.height);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        SCOPED_TRACE(describe(parts[i]));
        EXPECT_EQ(first_difference(whole, parts[i], got[i]), "");
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    RecordProperty("seconds", std::to_string(seconds));
}

}  // namespace
