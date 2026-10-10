/**
 * @file semver_test.cpp
 * @brief Offline tests for version parsing and ordering.
 */

#include <gtest/gtest.h>

#include "util/semver.h"

using lens::util::parseTag;
using lens::util::SemVer;

namespace {

/// Braced values inside a macro argument would be read as a second argument, so each one is
/// named before it is compared.
SemVer ver(int major, int minor, int patch)
{
    return SemVer{major, minor, patch};
}

} // namespace

TEST(SemVer, AcceptsATagWithOrWithoutTheLeadingV)
{
    EXPECT_EQ(parseTag("v1.2.0"), ver(1, 2, 0));
    EXPECT_EQ(parseTag("1.2.0"), ver(1, 2, 0));
    EXPECT_EQ(parseTag("v0.0.0"), ver(0, 0, 0));
}

TEST(SemVer, RejectsAnythingThatIsNotThreePlainNumbers)
{
    EXPECT_FALSE(parseTag("v1.2").has_value());
    EXPECT_FALSE(parseTag("1.2.0.1").has_value());
    EXPECT_FALSE(parseTag("1..0").has_value());
    EXPECT_FALSE(parseTag("a.b.c").has_value());
    EXPECT_FALSE(parseTag("").has_value());
    EXPECT_FALSE(parseTag("v").has_value());
    EXPECT_FALSE(parseTag("v.").has_value());
    EXPECT_FALSE(parseTag("1.2.").has_value());
}

TEST(SemVer, RejectsAPreReleaseOrABuildSuffixRatherThanTruncatingIt)
{
    // Truncating either of these would report a pre-release as the plain version before it,
    // which is the reader being told to update to something they already have.
    EXPECT_FALSE(parseTag("1.2.0-rc1").has_value());
    EXPECT_FALSE(parseTag("v1.2.0-rc.1").has_value());
    EXPECT_FALSE(parseTag("1.2.0+b1").has_value());
    EXPECT_FALSE(parseTag("1.2.0 ").has_value());
    EXPECT_FALSE(parseTag(" 1.2.0").has_value());
}

TEST(SemVer, RejectsAComponentThatDoesNotFitInAnInt)
{
    EXPECT_FALSE(parseTag("2147483648.0.0").has_value());
    EXPECT_FALSE(parseTag("99999999999999.0.0").has_value());
}

TEST(SemVer, OrdersByNumberNotBySpelling)
{
    EXPECT_TRUE(ver(1, 9, 0) < ver(1, 10, 0));
    EXPECT_TRUE(ver(2, 0, 0) > ver(1, 99, 99));
    EXPECT_TRUE(ver(1, 2, 3) > ver(1, 2, 2));
    EXPECT_FALSE(ver(1, 2, 0) > ver(1, 2, 0));
    EXPECT_TRUE(ver(1, 2, 0) == ver(1, 2, 0));
}