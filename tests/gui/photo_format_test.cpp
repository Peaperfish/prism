#include "prism/gui/photo_format.hpp"

#include <gtest/gtest.h>

namespace prism::gui {
namespace {

constexpr const char* dot = " \xc2\xb7 ";

TEST(PhotoFormat, FocalLengthIsWholeMillimetresFromTen) {
    EXPECT_EQ(format_focal(70.0, 70), "70 mm");
    EXPECT_EQ(format_focal(70.0), "70 mm");
    EXPECT_EQ(format_focal(24.4), "24 mm");
    EXPECT_EQ(format_focal(9.96), "10 mm");
}

TEST(PhotoFormat, FocalLengthUnderTenHasOneDecimal) {
    EXPECT_EQ(format_focal(6.8), "6.8 mm");
    EXPECT_EQ(format_focal(8.0), "8.0 mm");
}

TEST(PhotoFormat, EquivalentShowsOnlyWhenItDiffersByMoreThanTenPercent) {
    EXPECT_EQ(format_focal(6.765, 24), "6.8 mm (24 mm equiv.)");
    EXPECT_EQ(format_focal(50.0, 52), "50 mm");   // 4% off
    EXPECT_EQ(format_focal(50.0, 55), "50 mm");   // exactly 10% is not more
    EXPECT_EQ(format_focal(50.0, 56), "50 mm (56 mm equiv.)");
    EXPECT_EQ(format_focal(35.0, 0), "35 mm");
}

TEST(PhotoFormat, ApertureHasAtMostOneDecimal) {
    EXPECT_EQ(format_aperture(1.8), "f/1.8");
    EXPECT_EQ(format_aperture(1.78), "f/1.8");
    EXPECT_EQ(format_aperture(2.8), "f/2.8");
    EXPECT_EQ(format_aperture(10.0), "f/10");
    EXPECT_EQ(format_aperture(22.0), "f/22");
    EXPECT_EQ(format_aperture(4.0), "f/4");
    EXPECT_EQ(format_aperture(0.0), "");
}

TEST(PhotoFormat, ShutterIsAFractionWhenItIsNearOne) {
    EXPECT_EQ(format_shutter(0.004), "1/250 s");
    EXPECT_EQ(format_shutter(0.5), "1/2 s");
    EXPECT_EQ(format_shutter(1.0 / 3.0), "1/3 s");
    EXPECT_EQ(format_shutter(1.0 / 8000.0), "1/8000 s");
    EXPECT_EQ(format_shutter(0.0166667), "1/60 s");
}

TEST(PhotoFormat, ShutterOtherwiseIsSecondsWithAtMostOneDecimal) {
    EXPECT_EQ(format_shutter(0.6), "0.6 s");
    EXPECT_EQ(format_shutter(1.0), "1 s");
    EXPECT_EQ(format_shutter(2.0), "2 s");
    EXPECT_EQ(format_shutter(2.5), "2.5 s");
    EXPECT_EQ(format_shutter(30.0), "30 s");
    EXPECT_EQ(format_shutter(0.98), "1 s");
    EXPECT_EQ(format_shutter(0.0), "");
}

TEST(PhotoFormat, IsoIsLabelled) {
    EXPECT_EQ(format_iso(100), "ISO 100");
    EXPECT_EQ(format_iso(12800), "ISO 12800");
}

TEST(PhotoFormat, CameraLineOfAPhoneShot) {
    PhotoInfo info;
    info.make = "Apple";
    info.model = "iPhone 15 Pro";
    info.focal_length_mm = 6.765;
    info.focal_length_35mm = 24;
    info.f_number = 1.78;
    info.exposure_seconds = 1.0 / 120.0;
    info.iso = 50;
    EXPECT_EQ(camera_line(info), std::string("iPhone 15 Pro") + dot + "6.8 mm (24 mm equiv.)" + dot +
                                     "f/1.8" + dot + "1/120 s" + dot + "ISO 50");
}

TEST(PhotoFormat, CameraLineOfAFullFrameShotHasNoEquivalent) {
    PhotoInfo info;
    info.model = "ILCE-7RM5";
    info.focal_length_mm = 70.0;
    info.focal_length_35mm = 70;
    info.f_number = 2.8;
    info.exposure_seconds = 0.004;
    info.iso = 100;
    EXPECT_EQ(camera_line(info), std::string("ILCE-7RM5") + dot + "70 mm" + dot + "f/2.8" + dot +
                                     "1/250 s" + dot + "ISO 100");
}

TEST(PhotoFormat, CameraLineSkipsMissingFields) {
    PhotoInfo info;
    info.iso = 400;
    info.f_number = 4.0;
    EXPECT_EQ(camera_line(info), std::string("f/4") + dot + "ISO 400");
    info.make = "SONY";  // the make alone says nothing for the line
    EXPECT_EQ(camera_line(info), std::string("f/4") + dot + "ISO 400");
    EXPECT_EQ(camera_line(PhotoInfo{}), "");
}

TEST(PhotoFormat, TooltipHasOneLinePerKnownField) {
    PhotoInfo info;
    info.make = "SONY";
    info.model = "ILCE-7RM5";
    info.lens = "FE 24-70mm F2.8 GM";
    info.date_taken = "2024:05:06 07:08:09";
    EXPECT_EQ(camera_tooltip(info),
              "SONY\nILCE-7RM5\nFE 24-70mm F2.8 GM\nTaken 2024:05:06 07:08:09");
    info.make.reset();
    info.lens.reset();
    EXPECT_EQ(camera_tooltip(info), "ILCE-7RM5\nTaken 2024:05:06 07:08:09");
    EXPECT_EQ(camera_tooltip(PhotoInfo{}), "");
}

TEST(PhotoFormat, FileKindComesFromTheExtension) {
    EXPECT_EQ(file_kind_label("a.jpg"), "JPEG");
    EXPECT_EQ(file_kind_label("a.JPEG"), "JPEG");
    EXPECT_EQ(file_kind_label("a.png"), "PNG");
    EXPECT_EQ(file_kind_label("A.PNG"), "PNG");
    EXPECT_EQ(file_kind_label("IMG_1.HEIC"), "HEIC");
    EXPECT_EQ(file_kind_label("x.heif"), "HEIC");
    EXPECT_EQ(file_kind_label("DSC1.ARW"), "RAW");
    EXPECT_EQ(file_kind_label("a.dng"), "RAW");
}

}  // namespace
}  // namespace prism::gui
