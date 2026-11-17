// Writes the synthetic DNG of tests/synthetic_dng.hpp (32 x 32 Bayer pixels, every photosite at
// 13990, about 0.21 of white) to a file, for tools/make_sdr_goldens.sh, which renders the golden
// RAW references CI can check from it. Not part of the normal build (like make_heic_fixtures.cpp).
// Run from the repository root:
//
//   clang++ -std=c++20 -I tests -I lib tools/make_synthetic_dng.cpp -o /tmp/make_synthetic_dng
//   /tmp/make_synthetic_dng tests/data/golden/synthetic/synthetic.dng
//
// GoldenSdr.SyntheticDngIsTheStoredOne checks that the stored file still has the helper's bytes.
//
// Why 13990: the pictures are uniform grey, so off Apple arm64 a 1-level change fails (it is in
// every sample), and a LibRaw or compiler difference moves the decoded value by a code or two. At
// 16384 the bw look was 4 codes (0.005 of a level) from rounding to the next level; at 13990 no
// look changes for any level from 13955 to 14027, 35 codes below and 37 above. Keep it equal to
// synthetic_level in tests/golden_sdr_test.cpp.

#include <cstdio>
#include <fstream>
#include <vector>

#include "synthetic_dng.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <output.dng>\n", argv[0]);
        return 2;
    }
    const std::vector<std::byte> bytes = prism_test::synthetic_dng(13990);
    std::ofstream out(argv[1], std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        std::fprintf(stderr, "could not write %s\n", argv[1]);
        return 1;
    }
    return 0;
}
