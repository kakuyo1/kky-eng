/**
 * @file util_test.cpp
 * @brief Offline tests for the Qt-free shared utility boundary.
 */

#include <gtest/gtest.h>

#include "util/text.h"

TEST(UtilText, ExtractsTheDateFromAStoredMinute)
{
    EXPECT_EQ(lens::util::datePart("2026-10-03 14:20"), "2026-10-03");
    EXPECT_TRUE(lens::util::datePart("2026-10").empty());
}

TEST(UtilText, ClampedAddNeverProducesANegativeTally)
{
    EXPECT_EQ(lens::util::clampedAdd(4, -1), 3);
    EXPECT_EQ(lens::util::clampedAdd(0, -1), 0);
    EXPECT_EQ(lens::util::clampedAdd(4, 2), 6);
}
