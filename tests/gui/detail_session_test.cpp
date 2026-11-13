#include <gtest/gtest.h>

#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
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
#include "prism/gui/detail_base.hpp"
#include "prism/gui/detail_reader.hpp"
#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/raw.hpp"
#include "prism/stroke_field.hpp"
#include "synthetic_dng.hpp"

// The Session's detail base (#37): the full-size photo read in the background after opening, what
// restarts that read, and exports while it runs. As in background_export_test.cpp: no sleeps;
// the fake read below and the read's own signals decide the order of events, and the timeouts only
// matter if something is broken. Runs under ThreadSanitizer in CI.

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

using prism::Aspect;
using prism::FieldStroke;
using prism::Frame;
using prism::FrameBox;
using prism::Image;
using prism::Photo;
using prism::StrokeField;
using prism::gui::DetailBaseState;
using prism::gui::EditSettings;
using prism::gui::Error;
using prism::gui::ExportJob;
using prism::gui::InputKind;
using prism::gui::MaybeError;
using prism::gui::Session;
using prism::gui::SessionOptions;
using prism::gui::Strokes;

constexpr auto timeout = 30s;

// A photo larger than the 2560 preview on its long side (so the preview is shrunk and the detail
// base must be read), and one that fits.
constexpr int big_w = 2600;
constexpr int big_h = 1100;
constexpr int small_w = 1200;
constexpr int small_h = 800;

fs::path temp_file(const std::string& name) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    return fs::path(testing::TempDir()) /
           ("prism_detail_" + std::string(test->name()) + "_" + std::to_string(pid) + "_" + name);
}

// Removes the files it was given when the test ends.
class TempFiles {
public:
    fs::path add(const fs::path& path) {
        paths_.push_back(path);
        return path;
    }
    ~TempFiles() {
        for (const fs::path& p : paths_) {
            std::error_code ec;
            fs::remove(p, ec);
        }
    }

private:
    std::vector<fs::path> paths_;
};

// A width x height PNG whose every neighbourhood differs, so a misplaced pixel shows.
fs::path write_png(TempFiles& files, const std::string& name, int width, int height) {
    Image img;
    img.width = width;
    img.height = height;
    img.pixels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            img.pixels.push_back(static_cast<float>((x * 7 + y * 13) % 251) / 250.0f);
            img.pixels.push_back(static_cast<float>((x ^ y) % 256) / 255.0f);
            img.pixels.push_back(0.1f + static_cast<float>(y % 11) / 20.0f);
        }
    }
    const fs::path path = files.add(temp_file(name));
    prism::save_image(path, img);
    return path;
}

fs::path write_dng(TempFiles& files, const std::string& name) {
    const std::vector<std::byte> bytes = prism_test::synthetic_dng(16384);
    const fs::path path = files.add(temp_file(name));
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    return path;
}

std::vector<char> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<std::uint32_t> bits(const Image& img) {
    std::vector<std::uint32_t> out;
    out.reserve(img.pixels.size());
    for (const float v : img.pixels) {
        out.push_back(std::bit_cast<std::uint32_t>(v));
    }
    return out;
}

// `part` of `whole`, copied.
Image crop(const Image& whole, const FrameBox& part) {
    Image out;
    out.width = part.width;
    out.height = part.height;
    for (int y = part.y0; y < part.y0 + part.height; ++y) {
        const auto first = whole.pixels.begin() +
                           static_cast<std::ptrdiff_t>(
                               (static_cast<std::size_t>(y) * static_cast<std::size_t>(whole.width) +
                                static_cast<std::size_t>(part.x0)) *
                               3);
        out.pixels.insert(out.pixels.end(), first,
                          first + static_cast<std::ptrdiff_t>(part.width) * 3);
    }
    return out;
}

// Turned 7 degrees and cropped, with strokes across the photo, white balance and black and white:
// every stage that places pixels or strokes.
EditSettings look(const EditSettings& start, int w, int h) {
    EditSettings s = start;
    Frame f;
    f.angle = 7.0f;
    f.left = 0.05f;
    f.top = 0.08f;
    f.right = 0.93f;
    f.bottom = 0.9f;
    f.aspect = Aspect::free;
    s.frame = f;
    auto field = std::make_shared<StrokeField>();
    field->aspect = prism::aspect_of(w, h);
    field->strokes.push_back(std::make_shared<const FieldStroke>(
        FieldStroke{{{0.05f, 0.2f}, {0.5f, 0.55f}, {0.95f, 0.4f}}, 0.12f, 0.8f, -1.2f}));
    field->strokes.push_back(
        std::make_shared<const FieldStroke>(FieldStroke{{{0.45f, 0.6f}}, 0.2f, 1.0f, 1.5f}));
    field->strokes.push_back(std::make_shared<const FieldStroke>(
        FieldStroke{{{0.0f, 0.5f}, {1.0f, 0.52f}}, 0.03f, 0.0f, 0.7f}));
    s.strokes = Strokes(std::move(field));
    s.exposure_stops = 0.4f;
    s.white_balance = prism::WhiteBalance{5200.0, 8.0};
    s.saturation = 0.0f;
    s.curve = prism::gui::CurveKind::filmic;
    return s;
}

// What export makes from `source` before encoding: decode_for_export, then finish_export.
Image exported(const fs::path& source, InputKind kind, const EditSettings& s) {
    std::variant<Photo, Error> decoded = prism::gui::decode_for_export(source, kind, s, 0);
    if (const Error* e = std::get_if<Error>(&decoded)) {
        ADD_FAILURE() << "decode_for_export failed: " << e->message;
        return {};
    }
    std::variant<Image, Error> out =
        prism::gui::finish_export(std::move(std::get<Photo>(decoded)), s, std::nullopt, 0);
    if (const Error* e = std::get_if<Error>(&out)) {
        ADD_FAILURE() << "finish_export failed: " << e->message;
        return {};
    }
    return std::move(std::get<Image>(out));
}

Photo decoded_for_export(const fs::path& source, InputKind kind, const EditSettings& s) {
    std::variant<Photo, Error> decoded = prism::gui::decode_for_export(source, kind, s, 0);
    if (const Error* e = std::get_if<Error>(&decoded)) {
        ADD_FAILURE() << "decode_for_export failed: " << e->message;
        return {};
    }
    return std::move(std::get<Photo>(decoded));
}

// A read the test controls. Each call records what it was asked to read and its stop token,
// then waits until release() (or its stop), then, unless hold() was called, finishes at once;
// with hold() it then waits for finish() however often it is stopped, as a LibRaw decode that
// has begun runs to its end. It then decodes for real (decode_for_export, with no stop token).
class FakeReads {
public:
    prism::gui::DetailReadFunction function() {
        return [this](const fs::path& source, InputKind kind, const EditSettings& settings,
                      int threads, std::stop_token stop) -> std::variant<Photo, Error> {
            std::unique_lock lock(mutex_);
            calls_.push_back(Call{source, settings, stop});
            changed_.notify_all();
            changed_.wait(lock, stop, [&] { return released_; });
            changed_.wait(lock, [&] { return !held_ || finished_; });
            lock.unlock();
            return prism::gui::decode_for_export(source, kind, settings, threads);
        };
    }

    struct Call {
        fs::path source;
        EditSettings settings;
        std::stop_token stop;
    };

    // Blocks until at least `count` reads have begun; false if not within the timeout.
    bool wait_for_calls(std::size_t count) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] { return calls_.size() >= count; });
    }
    std::vector<Call> calls() {
        std::lock_guard lock(mutex_);
        return calls_;
    }
    void release() { set([&] { released_ = true; }); }
    void hold() { set([&] { held_ = true; }); }
    void finish() { set([&] { finished_ = true; }); }

private:
    template <typename F>
    void set(F&& change) {
        {
            std::lock_guard lock(mutex_);
            change();
        }
        changed_.notify_all();
    }

    std::mutex mutex_;
    std::condition_variable_any changed_;
    std::vector<Call> calls_;
    bool released_ = false;
    bool held_ = false;
    bool finished_ = false;
};

// Lets every read go when the test ends, even on a failed assertion, so that the Session declared
// before it never waits at its end for a read that is still held. Declare it after the Session.
class LetGo {
public:
    explicit LetGo(FakeReads& reads) : reads_(reads) {}
    ~LetGo() {
        reads_.release();
        reads_.finish();
    }
    LetGo(const LetGo&) = delete;
    LetGo& operator=(const LetGo&) = delete;

private:
    FakeReads& reads_;
};

SessionOptions with_detail(FakeReads* reads = nullptr) {
    SessionOptions o;
    o.detail_base = true;
    if (reads) {
        o.read_detail_base = reads->function();
    }
    return o;
}

}  // namespace

TEST(DetailSession, OffByDefault) {
    TempFiles files;
    Session session;
    EXPECT_EQ(session.detail_base_state(), DetailBaseState::off);
    ASSERT_EQ(session.open_file(write_png(files, "big.png", big_w, big_h)), std::nullopt);
    EXPECT_EQ(session.detail_base_state(), DetailBaseState::off);
    EXPECT_EQ(session.detail_base(), nullptr);
}

TEST(DetailSession, ALargePhotoIsReadAndRendersAsExportDoes) {
    TempFiles files;
    const fs::path source = write_png(files, "big.png", big_w, big_h);
    Session session(with_detail());
    ASSERT_EQ(session.open_file(source), std::nullopt);
    ASSERT_LT(session.preview_base()->width, big_w);  // the preview is shrunk
    ASSERT_TRUE(session.wait_for_detail_base(timeout));
    ASSERT_EQ(session.detail_base_state(), DetailBaseState::ready);
    const std::shared_ptr<const Photo> base = session.detail_base();
    ASSERT_NE(base, nullptr);
    ASSERT_EQ(base->image.width, big_w);
    ASSERT_EQ(base->image.height, big_h);

    const EditSettings s = look(session.settings(), big_w, big_h);
    ASSERT_EQ(session.set_settings(s), std::nullopt);
    const Image whole = exported(source, InputKind::image, s);
    ASSERT_GT(whole.width, 0);
    // A part across the middle (strokes, the frame's turn), the top-left corner and an odd strip.
    for (const FrameBox& part :
         {FrameBox{whole.width / 2 - 150, whole.height / 2 - 100, 300, 200}, FrameBox{0, 0, 97, 61},
          FrameBox{whole.width - 333, whole.height - 71, 333, 71}}) {
        const Image region = prism::gui::render_region(*session.detail_base(), s, part);
        EXPECT_EQ(bits(region), bits(crop(whole, part)))
            << "part (" << part.x0 << ", " << part.y0 << ") " << part.width << " x "
            << part.height << " of export's " << whole.width << " x " << whole.height;
    }
}

TEST(DetailSession, APhotoThatFitsThePreviewReadsNothing) {
    TempFiles files;
    const fs::path source = write_png(files, "small.png", small_w, small_h);
    FakeReads reads;
    Session session(with_detail(&reads));
    const LetGo let_go(reads);
    ASSERT_EQ(session.open_file(source), std::nullopt);
    EXPECT_EQ(session.detail_base_state(), DetailBaseState::not_needed);
    const std::shared_ptr<const Photo> base = session.detail_base();
    ASSERT_NE(base, nullptr);
    // The preview base's own pixels, not a copy.
    EXPECT_EQ(&base->image, session.whole_base().get());

    const EditSettings s = look(session.settings(), small_w, small_h);
    ASSERT_EQ(session.set_settings(s), std::nullopt);
    const Image whole = exported(source, InputKind::image, s);
    ASSERT_GT(whole.width, 0);
    const Image all = prism::gui::render_region(*session.detail_base(), s,
                                                FrameBox{0, 0, whole.width, whole.height});
    EXPECT_EQ(bits(all), bits(whole));

    ASSERT_TRUE(session.wait_for_detail_base(timeout));
    EXPECT_TRUE(reads.calls().empty());
}

TEST(DetailSession, ARawPreviewIsTheBaseOnlyAtHalfSize) {
    if (!prism::raw_support_available()) {
        GTEST_SKIP() << "needs LibRaw";
    }
    TempFiles files;
    const fs::path source = write_dng(files, "shot.dng");
    Session session(with_detail());
    ASSERT_EQ(session.open_file(source), std::nullopt);
    // The preview is the half-size decode; a full-size export reads the file again.
    ASSERT_FALSE(session.settings().raw_half_size);
    EXPECT_NE(session.detail_base_state(), DetailBaseState::not_needed);
    ASSERT_TRUE(session.wait_for_detail_base(timeout));
    ASSERT_EQ(session.detail_base_state(), DetailBaseState::ready);
    EXPECT_EQ(bits(session.detail_base()->image),
              bits(decoded_for_export(source, InputKind::raw, session.settings()).image));
    EXPECT_EQ(session.detail_base()->image.width, prism_test::synthetic_dng_size);

    // With raw_half_size on, the export's decode is the preview's: nothing to read.
    EditSettings half = session.settings();
    half.raw_half_size = true;
    ASSERT_EQ(session.set_settings(half), std::nullopt);
    EXPECT_EQ(session.detail_base_state(), DetailBaseState::not_needed);
    ASSERT_NE(session.detail_base(), nullptr);
    EXPECT_EQ(&session.detail_base()->image, session.whole_base().get());
    EXPECT_EQ(bits(session.detail_base()->image),
              bits(decoded_for_export(source, InputKind::raw, half).image));
}

TEST(DetailSession, SettingsThatDontChangeTheDecodeDontRestartTheRead) {
    TempFiles files;
    const fs::path source = write_png(files, "big.png", big_w, big_h);
    FakeReads reads;
    Session session(with_detail(&reads));
    const LetGo let_go(reads);
    ASSERT_EQ(session.open_file(source), std::nullopt);
    ASSERT_TRUE(reads.wait_for_calls(1));
    EXPECT_EQ(session.detail_base_state(), DetailBaseState::reading);

    EditSettings s = session.settings();
    s.exposure_stops = 1.0f;
    ASSERT_EQ(session.set_settings(s), std::nullopt);
    s = look(s, big_w, big_h);
    ASSERT_EQ(session.set_settings(s), std::nullopt);
    // raw_half_size changes nothing that decode_for_export reads for a PNG.
    s.raw_half_size = true;
    ASSERT_EQ(session.set_settings(s), std::nullopt);
    ASSERT_EQ(session.undo(), std::nullopt);

    reads.release();
    ASSERT_TRUE(session.wait_for_detail_base(timeout));
    EXPECT_EQ(session.detail_base_state(), DetailBaseState::ready);
    // A restarted read would have stopped the first and begun a second before ending.
    const std::vector<FakeReads::Call> calls = reads.calls();
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_FALSE(calls[0].stop.stop_requested());
}

TEST(DetailSession, ACameraMatrixChangeRestartsTheRead) {
    if (!prism::raw_support_available()) {
        GTEST_SKIP() << "needs LibRaw";
    }
    TempFiles files;
    const fs::path source = write_dng(files, "shot.dng");
    FakeReads reads;
    Session session(with_detail(&reads));
    const LetGo let_go(reads);
    ASSERT_EQ(session.open_file(source), std::nullopt);
    ASSERT_TRUE(reads.wait_for_calls(1));

    EditSettings s = session.settings();
    prism::gui::MatrixFile matrix;
    matrix.path = "half.txt";
    matrix.values = {0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f};
    s.raw_camera_matrix = matrix;
    ASSERT_EQ(session.set_settings(s), std::nullopt);
    // The first read is stopped and a second, with the matrix, begins once it has ended.
    ASSERT_TRUE(reads.wait_for_calls(2));
    std::vector<FakeReads::Call> calls = reads.calls();
    EXPECT_TRUE(calls[0].stop.stop_requested());
    EXPECT_EQ(calls[1].settings.raw_camera_matrix, matrix);
    EXPECT_EQ(session.detail_base_state(), DetailBaseState::reading);

    reads.release();
    ASSERT_TRUE(session.wait_for_detail_base(timeout));
    ASSERT_EQ(session.detail_base_state(), DetailBaseState::ready);
    EXPECT_EQ(bits(session.detail_base()->image),
              bits(decoded_for_export(source, InputKind::raw, s).image));
    EXPECT_EQ(reads.calls().size(), 2u);
}

TEST(DetailSession, AnotherPhotoRestartsTheReadAndDropsTheOldResult) {
    TempFiles files;
    const fs::path first = write_png(files, "first.png", big_w, big_h);
    const fs::path second = write_png(files, "second.png", big_w + 40, big_h + 30);
    FakeReads reads;
    Session session(with_detail(&reads));
    const LetGo let_go(reads);
    ASSERT_EQ(session.open_file(first), std::nullopt);
    reads.release();
    // The first photo's read has finished and posted its result, which nobody has taken yet.
    ASSERT_TRUE(session.wait_for_detail_read(timeout));

    ASSERT_EQ(session.open_file(second), std::nullopt);
    EXPECT_EQ(session.detail_base_state(), DetailBaseState::reading);
    EXPECT_EQ(session.detail_base(), nullptr);
    ASSERT_TRUE(session.wait_for_detail_base(timeout));
    ASSERT_EQ(session.detail_base_state(), DetailBaseState::ready);
    EXPECT_EQ(session.detail_base()->image.width, big_w + 40);
    EXPECT_EQ(session.detail_base()->image.height, big_h + 30);
    const std::vector<FakeReads::Call> calls = reads.calls();
    ASSERT_EQ(calls.size(), 2u);
    EXPECT_EQ(calls[1].source, second);
}

TEST(DetailSession, AnotherPhotoWhileReadingStopsTheOldRead) {
    TempFiles files;
    const fs::path first = write_png(files, "first.png", big_w, big_h);
    const fs::path second = write_png(files, "second.png", big_w + 40, big_h + 30);
    FakeReads reads;
    Session session(with_detail(&reads));
    const LetGo let_go(reads);
    ASSERT_EQ(session.open_file(first), std::nullopt);
    ASSERT_TRUE(reads.wait_for_calls(1));
    ASSERT_EQ(session.open_file(second), std::nullopt);
    // The first read is stopped; it decodes anyway (as LibRaw would), and only then the second
    // begins.
    ASSERT_TRUE(reads.wait_for_calls(2));
    EXPECT_TRUE(reads.calls()[0].stop.stop_requested());
    reads.release();
    ASSERT_TRUE(session.wait_for_detail_base(timeout));
    ASSERT_EQ(session.detail_base_state(), DetailBaseState::ready);
    EXPECT_EQ(session.detail_base()->image.width, big_w + 40);
}

TEST(DetailSession, TooBigForMemoryReadsNothing) {
    TempFiles files;
    FakeReads reads;
    SessionOptions options = with_detail(&reads);
    // A quarter of this is less than the 2600 x 1100 photo's 34 MB.
    options.physical_memory = 100'000'000;
    Session session(options);
    const LetGo let_go(reads);
    ASSERT_EQ(session.open_file(write_png(files, "big.png", big_w, big_h)), std::nullopt);
    EXPECT_EQ(session.detail_base_state(), DetailBaseState::too_big);
    EXPECT_EQ(session.detail_base(), nullptr);
    ASSERT_TRUE(session.wait_for_detail_base(timeout));
    EXPECT_TRUE(reads.calls().empty());

    // A photo that fits the preview is in memory already, however little there is.
    ASSERT_EQ(session.open_file(write_png(files, "small.png", small_w, small_h)), std::nullopt);
    EXPECT_EQ(session.detail_base_state(), DetailBaseState::not_needed);
}

TEST(DetailSession, ExportWhileReadingWaitsForTheReadAndWritesTheSameFile) {
    TempFiles files;
    const fs::path source = write_png(files, "big.png", big_w, big_h);

    // The same export with no read anywhere.
    const fs::path alone = files.add(temp_file("alone.png"));
    {
        Session plain;
        ASSERT_EQ(plain.open_file(source), std::nullopt);
        ASSERT_EQ(plain.set_settings(look(plain.settings(), big_w, big_h)), std::nullopt);
        ASSERT_EQ(prism::gui::export_image(plain.export_job(), alone), std::nullopt);
    }

    FakeReads reads;
    reads.hold();  // once stopped, the read still finishes its decode, as LibRaw's would
    Session session(with_detail(&reads));
    const LetGo let_go(reads);
    ASSERT_EQ(session.open_file(source), std::nullopt);
    ASSERT_EQ(session.set_settings(look(session.settings(), big_w, big_h)), std::nullopt);
    ASSERT_TRUE(reads.wait_for_calls(1));

    const ExportJob job = session.begin_export();
    ASSERT_NE(job.read_ended, nullptr);
    EXPECT_TRUE(reads.calls()[0].stop.stop_requested());  // stopped before export begins
    EXPECT_EQ(session.detail_base_state(), DetailBaseState::reading);

    const fs::path out = files.add(temp_file("out.png"));
    MaybeError result = Error{};
    // With the jthread's stop token, so that a failed assertion below ends the export (its
    // destructor stops it) rather than leaving it waiting for a read that is held.
    std::jthread exporter(
        [&](std::stop_token stop) { result = prism::gui::export_image(job, out, stop); });
    // The export waits for the read to end before it decodes.
    ASSERT_TRUE(job.read_ended->wait_for_waiters(1, timeout));
    EXPECT_FALSE(job.read_ended->is_set());
    reads.finish();
    exporter.join();
    EXPECT_TRUE(job.read_ended->is_set());
    ASSERT_EQ(result, std::nullopt);
    EXPECT_EQ(read_file(out), read_file(alone));

    // No read starts during the export; once it has ended the read starts again.
    EXPECT_EQ(reads.calls().size(), 1u);
    session.end_export();
    ASSERT_TRUE(reads.wait_for_calls(2));
    reads.release();
    ASSERT_TRUE(session.wait_for_detail_base(timeout));
    EXPECT_EQ(session.detail_base_state(), DetailBaseState::ready);
}

TEST(DetailSession, ExportWithTheBaseReadyKeepsIt) {
    TempFiles files;
    const fs::path source = write_png(files, "big.png", big_w, big_h);
    const fs::path alone = files.add(temp_file("alone.png"));
    {
        Session plain;
        ASSERT_EQ(plain.open_file(source), std::nullopt);
        ASSERT_EQ(plain.set_settings(look(plain.settings(), big_w, big_h)), std::nullopt);
        ASSERT_EQ(prism::gui::export_image(plain.export_job(), alone), std::nullopt);
    }

    FakeReads reads;
    Session session(with_detail(&reads));
    const LetGo let_go(reads);
    ASSERT_EQ(session.open_file(source), std::nullopt);
    ASSERT_EQ(session.set_settings(look(session.settings(), big_w, big_h)), std::nullopt);
    reads.release();
    ASSERT_TRUE(session.wait_for_detail_base(timeout));
    const std::shared_ptr<const Photo> base = session.detail_base();
    ASSERT_NE(base, nullptr);

    const ExportJob job = session.begin_export();
    const fs::path out = files.add(temp_file("out.png"));
    ASSERT_EQ(prism::gui::export_image(job, out), std::nullopt);
    session.end_export();
    EXPECT_EQ(read_file(out), read_file(alone));
    EXPECT_EQ(session.detail_base_state(), DetailBaseState::ready);
    EXPECT_EQ(session.detail_base(), base);
    EXPECT_EQ(reads.calls().size(), 1u);
}

TEST(DetailSession, AnExportEndedForClosingStartsNoRead) {
    TempFiles files;
    const fs::path source = write_png(files, "big.png", big_w, big_h);
    FakeReads reads;  // never released: a read that starts stays running until stopped
    Session session(with_detail(&reads));
    const LetGo let_go(reads);
    ASSERT_EQ(session.open_file(source), std::nullopt);
    ASSERT_TRUE(reads.wait_for_calls(1));
    const ExportJob job = session.begin_export();  // stops the read
    ASSERT_TRUE(session.wait_for_detail_read(timeout));
    session.end_export(false);
    // Nothing running: no read was started.
    EXPECT_TRUE(session.wait_for_detail_read(std::chrono::milliseconds(0)));
    EXPECT_EQ(reads.calls().size(), 1u);
    // An ordinary end does start it.
    const ExportJob again = session.begin_export();
    session.end_export();
    EXPECT_TRUE(reads.wait_for_calls(2));
}

TEST(DetailSession, ClosingWhileReadingStopsTheRead) {
    TempFiles files;
    FakeReads reads;
    std::stop_token stop;
    {
        Session session(with_detail(&reads));
        ASSERT_EQ(session.open_file(write_png(files, "big.png", big_w, big_h)), std::nullopt);
        ASSERT_TRUE(reads.wait_for_calls(1));
        stop = reads.calls()[0].stop;
        EXPECT_FALSE(stop.stop_requested());
    }  // the destructor stops the read and waits for it
    EXPECT_TRUE(stop.stop_requested());
}

TEST(DetailSession, CancellingAnExportWaitingForTheReadWritesNothing) {
    TempFiles files;
    const fs::path source = write_png(files, "big.png", big_w, big_h);
    FakeReads reads;
    reads.hold();
    Session session(with_detail(&reads));
    const LetGo let_go(reads);
    ASSERT_EQ(session.open_file(source), std::nullopt);
    ASSERT_TRUE(reads.wait_for_calls(1));
    const ExportJob job = session.begin_export();
    const fs::path out = files.add(temp_file("out.png"));
    MaybeError result;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    std::jthread exporter([&](std::stop_token stop) {
        MaybeError r = prism::gui::export_image(job, out, stop);
        std::lock_guard lock(mutex);
        result = std::move(r);
        done = true;
        cv.notify_all();
    });
    ASSERT_TRUE(job.read_ended->wait_for_waiters(1, timeout));
    exporter.request_stop();
    {
        // The export ends on its stop while the read is still held.
        std::unique_lock lock(mutex);
        EXPECT_TRUE(cv.wait_for(lock, timeout, [&] { return done; }));
    }
    EXPECT_FALSE(job.read_ended->is_set());
    reads.finish();
    exporter.join();
    ASSERT_NE(result, std::nullopt);
    EXPECT_EQ(result->kind, prism::gui::ErrorKind::cancelled);
    EXPECT_FALSE(fs::exists(out));
    session.end_export();
}

// The real RAW file: the base read in the background is decode_for_export's, at full size and, with
// raw_half_size on, at half size (still larger than the preview, so read too).
TEST(DetailSession, TheRawFilesBaseIsExportsDecode) {
    const std::string raw = prism_test::env_var("PRISM_TEST_RAW");
    if (raw.empty()) {
        GTEST_SKIP() << "set PRISM_TEST_RAW to a RAW file to run this test";
    }
    if (!prism::raw_support_available()) {
        GTEST_SKIP() << "Prism was built without LibRaw";
    }
    const fs::path source(raw);
    Session session(with_detail());
    ASSERT_EQ(session.open_file(source), std::nullopt);
    for (const bool half : {false, true}) {
        EditSettings s = session.settings();
        s.raw_half_size = half;
        ASSERT_EQ(session.set_settings(s), std::nullopt);
        ASSERT_TRUE(session.wait_for_detail_base(timeout));
        ASSERT_EQ(session.detail_base_state(), DetailBaseState::ready) << "half size " << half;
        const std::shared_ptr<const Photo> base = session.detail_base();
        EXPECT_EQ(base->image.width, session.export_start_size().width);
        EXPECT_EQ(base->image.height, session.export_start_size().height);
        const Photo want = decoded_for_export(source, InputKind::raw, s);
        EXPECT_TRUE(base->image.pixels == want.image.pixels) << "half size " << half;
        EXPECT_EQ(base->info.as_shot_white_xy, want.info.as_shot_white_xy);
    }
}

TEST(DetailSession, AReadThatFinishedBeforeTheExportIsKept) {
    TempFiles files;
    FakeReads reads;
    Session session(with_detail(&reads));
    const LetGo let_go(reads);
    ASSERT_EQ(session.open_file(write_png(files, "big.png", big_w, big_h)), std::nullopt);
    reads.release();
    // The read has posted its photo, which nobody has taken yet, when the export begins.
    ASSERT_TRUE(session.wait_for_detail_read(timeout));
    const ExportJob job = session.begin_export();
    EXPECT_EQ(session.detail_base_state(), DetailBaseState::ready);
    EXPECT_NE(session.detail_base(), nullptr);
    session.end_export();
    ASSERT_TRUE(session.wait_for_detail_base(timeout));
    EXPECT_EQ(session.detail_base_state(), DetailBaseState::ready);
    EXPECT_EQ(reads.calls().size(), 1u);  // not read again
}

TEST(DetailSession, AFailedOpenReadsTheOldPhotosBaseAgain) {
    TempFiles files;
    const fs::path source = write_png(files, "big.png", big_w, big_h);
    const fs::path broken = files.add(temp_file("broken.png"));
    {
        std::ofstream out(broken, std::ios::binary);
        out << "not a PNG";
    }
    Session session(with_detail());
    ASSERT_EQ(session.open_file(source), std::nullopt);
    ASSERT_TRUE(session.wait_for_detail_base(timeout));
    const std::weak_ptr<const Photo> old = session.detail_base();
    ASSERT_FALSE(old.expired());

    // The old base was let go before the decode; the open fails, and the old photo stays open.
    const MaybeError error = session.open_file(broken);
    ASSERT_NE(error, std::nullopt);
    EXPECT_EQ(session.path(), source);
    EXPECT_TRUE(old.expired());
    ASSERT_TRUE(session.wait_for_detail_base(timeout));
    ASSERT_EQ(session.detail_base_state(), DetailBaseState::ready);
    EXPECT_EQ(session.detail_base()->image.width, big_w);
}

// DetailReader itself: a read decodes only once every read started before it has ended, so two
// full-size photos are never decoded at once.
TEST(DetailReader, AReadWaitsForTheReadBeforeIt) {
    TempFiles files;
    const fs::path first = write_png(files, "first.png", small_w, small_h);
    const fs::path second = write_png(files, "second.png", small_w + 10, small_h);
    FakeReads reads;
    reads.hold();  // the first read, once stopped, still runs until finish()
    prism::gui::DetailReader reader(reads.function());
    const LetGo let_go(reads);
    reader.start(1, first, InputKind::image, EditSettings{}, 0);
    ASSERT_TRUE(reads.wait_for_calls(1));
    const std::shared_ptr<const prism::gui::ReadEnd> first_end = reader.ended();

    reader.start(2, second, InputKind::image, EditSettings{}, 0);
    EXPECT_TRUE(reads.calls()[0].stop.stop_requested());
    // The second read is waiting for the first to end, and has not begun reading.
    ASSERT_TRUE(first_end->wait_for_waiters(1, timeout));
    EXPECT_EQ(reads.calls().size(), 1u);

    reads.finish();
    reads.release();
    ASSERT_TRUE(reads.wait_for_calls(2));
    EXPECT_EQ(reads.calls()[1].source, second);
    ASSERT_TRUE(reader.wait(timeout));
    const std::vector<prism::gui::DetailReader::Result> results = reader.take_all();
    ASSERT_EQ(results.size(), 1u);  // the stopped first read posted nothing
    EXPECT_EQ(results[0].generation, 2u);
    EXPECT_TRUE(first_end->is_set());
}
