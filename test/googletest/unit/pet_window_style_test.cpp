/**
 * @file pet_window_style_test.cpp
 * @brief The extended window style that mouse pass-through switches: only transparency moves, the layered bit and
 *        every other style stay (PHASE3 3.5).
 */

#include <cstdint>

#include <gtest/gtest.h>
#include <windows.h>

#include "app/pet/pet_window_native.h"

namespace {

using lens::app::pet::passthroughStyle;

constexpr auto kTransparent = static_cast<std::intptr_t>(WS_EX_TRANSPARENT);
constexpr auto kLayered     = static_cast<std::intptr_t>(WS_EX_LAYERED);
constexpr auto kToolWindow  = static_cast<std::intptr_t>(WS_EX_TOOLWINDOW);
constexpr auto kTopmost     = static_cast<std::intptr_t>(WS_EX_TOPMOST);

TEST(PetWindowStyle, PassThroughAddsTransparencyOverLayeredAndKeepsEverythingElse)
{
    auto const after = passthroughStyle(kToolWindow | kTopmost, true);

    EXPECT_NE(after & kTransparent, 0);
    EXPECT_NE(after & kLayered, 0);
    EXPECT_NE(after & kToolWindow, 0);
    EXPECT_NE(after & kTopmost, 0);
}

TEST(PetWindowStyle, TakingPassThroughBackClearsOnlyTransparency)
{
    auto const after = passthroughStyle(kLayered | kTransparent | kToolWindow, false);

    EXPECT_EQ(after & kTransparent, 0);
    EXPECT_NE(after & kLayered, 0);
    EXPECT_NE(after & kToolWindow, 0);
}

} // namespace
