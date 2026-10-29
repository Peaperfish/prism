#include <gtest/gtest.h>

#include "prism/version.hpp"

TEST(Version, IsNotEmpty) {
    EXPECT_FALSE(prism::version().empty());
}
