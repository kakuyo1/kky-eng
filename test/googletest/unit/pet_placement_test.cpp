/**
 * @file pet_placement_test.cpp
 * @brief Where the pet window goes when the screen under it changed: kept if it fits, else the bottom-right (PHASE3 3.5).
 */

#include <gtest/gtest.h>

#include <QPoint>
#include <QRect>
#include <QSize>

#include "app/pet/pet_placement.h"

namespace {

using lens::app::pet::bottomRightOf;
using lens::app::pet::fitsWithin;

/// A 1920x1040 usable area, starting at the origin.
QRect const kArea{0, 0, 1920, 1040};
QSize const kPet{264, 264};

TEST(PetPlacement, AWindowInsideTheAreaFits)
{
    EXPECT_TRUE(fitsWithin(QPoint{100, 100}, kPet, kArea));
}

TEST(PetPlacement, AWindowHangingOffAnEdgeDoesNotFit)
{
    EXPECT_FALSE(fitsWithin(QPoint{1800, 100}, kPet, kArea));
}

TEST(PetPlacement, TheBottomRightCornerIsInsideTheArea)
{
    auto const at = bottomRightOf(kPet, kArea);

    EXPECT_EQ(at, (QPoint{1920 - 264, 1040 - 264}));
    EXPECT_TRUE(fitsWithin(at, kPet, kArea));
}

} // namespace
