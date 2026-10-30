#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

#include "prism/gui/file_types.hpp"
#include "prism/raw.hpp"
#include "prism/utf8_path.hpp"

namespace fs = std::filesystem;
using prism::gui::default_export_format;
using prism::gui::ExportFormat;
using prism::gui::open_filter_pattern;
using prism::gui::suggested_export_path;
using prism::gui::with_export_extension;

TEST(FileTypes, OpenFilterOffersRawOnlyWithLibRaw) {
    const std::string pattern = open_filter_pattern();
    EXPECT_EQ(pattern.rfind("png;jpg;jpeg", 0), 0u);
    if (prism::raw_support_available()) {
        EXPECT_EQ(pattern, "png;jpg;jpeg;arw;dng;cr2;cr3;nef;raf");
    } else {
        EXPECT_EQ(pattern, "png;jpg;jpeg");
    }
}

TEST(FileTypes, OpenFilterMatchesWhatIsRawFileAccepts) {
    // Every RAW extension the dialog offers is one Session will treat as RAW.
    const std::string pattern = open_filter_pattern();
    std::size_t start = 0;
    while (start < pattern.size()) {
        const std::size_t end = std::min(pattern.find(';', start), pattern.size());
        const std::string ext = pattern.substr(start, end - start);
        const bool image = ext == "png" || ext == "jpg" || ext == "jpeg";
        EXPECT_EQ(prism::is_raw_file("x." + ext), !image) << ext;
        start = end + 1;
    }
}

TEST(FileTypes, KeepsAnExportExtensionTheUserTyped) {
    EXPECT_EQ(with_export_extension("out/photo.png", ExportFormat::jpeg), fs::path("out/photo.png"));
    EXPECT_EQ(with_export_extension("photo.JPG", ExportFormat::png), fs::path("photo.JPG"));
    EXPECT_EQ(with_export_extension("photo.jpeg", ExportFormat::png), fs::path("photo.jpeg"));
}

TEST(FileTypes, AddsTheChosenFormatsExtensionOtherwise) {
    EXPECT_EQ(with_export_extension("photo", ExportFormat::jpeg), fs::path("photo.jpg"));
    EXPECT_EQ(with_export_extension("photo", ExportFormat::png), fs::path("photo.png"));
    EXPECT_EQ(with_export_extension("photo.v2", ExportFormat::jpeg), fs::path("photo.v2.jpg"));
    EXPECT_EQ(with_export_extension("photo.tiff", ExportFormat::png), fs::path("photo.tiff.png"));
}

TEST(FileTypes, NonAsciiNamesSurvive) {
    const fs::path chosen = prism::path_from_utf8("写真/prism_ü");
    EXPECT_EQ(prism::utf8(with_export_extension(chosen, ExportFormat::jpeg)), "写真/prism_ü.jpg");
}

TEST(FileTypes, SuggestsANameNextToTheSource) {
    EXPECT_EQ(suggested_export_path(fs::path("shots") / "DSC0001.ARW", ExportFormat::jpeg),
              fs::path("shots") / "DSC0001_prism.jpg");
    EXPECT_EQ(suggested_export_path("photo.png", ExportFormat::png), fs::path("photo_prism.png"));
}

TEST(FileTypes, ExportsPngAsPngAndEverythingElseAsJpeg) {
    EXPECT_EQ(default_export_format("a.png"), ExportFormat::png);
    EXPECT_EQ(default_export_format("a.PNG"), ExportFormat::png);
    EXPECT_EQ(default_export_format("a.jpg"), ExportFormat::jpeg);
    EXPECT_EQ(default_export_format("a.ARW"), ExportFormat::jpeg);
}
