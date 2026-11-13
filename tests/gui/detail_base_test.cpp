#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "prism/gui/detail_base.hpp"
#include "prism/gui/system_memory.hpp"

// The detail base's pure decisions (#37): whether a photo's full-size base is read, and where
// that leaves the UI; and the physical memory the decision is measured against.

namespace {

using prism::PixelSize;
using prism::gui::detail_base_state;
using prism::gui::DetailBaseState;
using prism::gui::DetailPlan;
using prism::gui::full_size_bytes;
using prism::gui::plan_detail_base;

constexpr std::uint64_t gib = std::uint64_t{1} << 30;

TEST(DetailBase, FullSizeBytesIsThreeFloatsAPixel) {
    EXPECT_EQ(full_size_bytes({9564, 6376}), std::uint64_t{9564} * 6376 * 12);
    EXPECT_EQ(full_size_bytes({1, 1}), 12u);
    EXPECT_EQ(full_size_bytes({0, 10}), 0u);
    const int big = std::numeric_limits<int>::max();
    EXPECT_EQ(full_size_bytes({big, big}), std::numeric_limits<std::uint64_t>::max());
}

TEST(DetailBase, APreviewThatIsTheExportDecodeNeedsNothingRead) {
    // A 2000 x 1500 PNG: the preview base is its decode, not reduced.
    EXPECT_EQ(plan_detail_base({2000, 1500}, {2000, 1500}, true, 16 * gib),
              DetailPlan::not_needed);
    // Even when it would be too big to read: it is already in memory.
    EXPECT_EQ(plan_detail_base({2000, 1500}, {2000, 1500}, true, 1), DetailPlan::not_needed);
}

TEST(DetailBase, AReducedPreviewIsRead) {
    EXPECT_EQ(plan_detail_base({6000, 4000}, {2560, 1707}, true, 16 * gib), DetailPlan::read);
}

TEST(DetailBase, AHalfSizeRawPreviewIsNotTheFullSizeExport) {
    // A RAW preview is the half-size decode: for a full-size export it is another decode, even
    // where its size happens to be the export's.
    EXPECT_EQ(plan_detail_base({2400, 1600}, {1200, 800}, false, 16 * gib), DetailPlan::read);
    EXPECT_EQ(plan_detail_base({2400, 1600}, {2400, 1600}, false, 16 * gib), DetailPlan::read);
    // With raw_half_size on, the export decodes at half size too: the same decode.
    EXPECT_EQ(plan_detail_base({1200, 800}, {1200, 800}, true, 16 * gib), DetailPlan::not_needed);
}

TEST(DetailBase, TooBigIsMoreThanAQuarterOfMemory) {
    const PixelSize size{9564, 6376};
    const PixelSize preview{2560, 1707};
    const std::uint64_t bytes = full_size_bytes(size);
    // A quarter of memory exactly the photo's bytes: it fits.
    EXPECT_EQ(plan_detail_base(size, preview, false, bytes * 4), DetailPlan::read);
    // A quarter one byte more than the photo: it fits.
    EXPECT_EQ(plan_detail_base(size, preview, false, (bytes + 1) * 4), DetailPlan::read);
    // A quarter one byte less than the photo: too big.
    EXPECT_EQ(plan_detail_base(size, preview, false, (bytes - 1) * 4), DetailPlan::too_big);
    // Memory one byte short of four times the photo: a quarter is a fraction of a byte under it.
    EXPECT_EQ(plan_detail_base(size, preview, false, bytes * 4 - 1), DetailPlan::too_big);
    EXPECT_EQ(plan_detail_base(size, preview, false, 2 * gib), DetailPlan::too_big);
    EXPECT_EQ(plan_detail_base(size, preview, false, 16 * gib), DetailPlan::read);
}

TEST(DetailBase, UnknownMemoryReads) {
    EXPECT_EQ(plan_detail_base({9564, 6376}, {2560, 1707}, false, 0), DetailPlan::read);
}

TEST(DetailBase, StateFollowsThePlanAndTheRead) {
    EXPECT_EQ(detail_base_state(false, DetailPlan::read, true, false), DetailBaseState::off);
    EXPECT_EQ(detail_base_state(true, DetailPlan::not_needed, false, false),
              DetailBaseState::not_needed);
    EXPECT_EQ(detail_base_state(true, DetailPlan::too_big, false, false),
              DetailBaseState::too_big);
    EXPECT_EQ(detail_base_state(true, DetailPlan::read, false, false), DetailBaseState::reading);
    EXPECT_EQ(detail_base_state(true, DetailPlan::read, true, false), DetailBaseState::ready);
    EXPECT_EQ(detail_base_state(true, DetailPlan::read, false, true), DetailBaseState::failed);
    // Only a read can be ready or fail.
    EXPECT_EQ(detail_base_state(true, DetailPlan::too_big, true, true), DetailBaseState::too_big);
}

TEST(SystemMemory, PhysicalMemoryIsKnown) {
    // Every CI platform (macOS, Windows, Linux) says how much memory it has.
    EXPECT_GT(prism::gui::physical_memory_bytes(), 0u);
}

}  // namespace
