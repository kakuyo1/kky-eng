/**
 * @file pet_accessory_test.cpp
 * @brief Wardrobe, offline: one accessory per slot, and per-action visibility (PHASE3 3.4, 3.9).
 */

#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/pet/accessory.h"

namespace {

using lens::core::pet::Action;
using lens::core::pet::AccessorySpec;
using lens::core::pet::Slot;
using lens::core::pet::Wardrobe;

/// Every action but sleep: sleep hides all accessories (PHASE3 3.4).
std::vector<Action> awakeActions()
{
    return {Action::Idle, Action::Study, Action::Thinking, Action::Celebrate, Action::Encourage, Action::ClickReact, Action::Pickup, Action::LookAround, Action::Yawn, Action::Stretch};
}

Wardrobe makeWardrobe()
{
    return Wardrobe{{
        AccessorySpec{"hat", Slot::Head, awakeActions()},
        AccessorySpec{"beanie", Slot::Head, awakeActions()},
        AccessorySpec{"glasses", Slot::Face, awakeActions()},
        AccessorySpec{"bowtie", Slot::Body, awakeActions()},
    }};
}

TEST(Wardrobe, WearingAnAccessoryPutsItOnItsSlot)
{
    auto wardrobe = makeWardrobe();
    wardrobe.toggle("hat");
    EXPECT_EQ(wardrobe.wornIn(Slot::Head), "hat");
    EXPECT_FALSE(wardrobe.wornIn(Slot::Face).has_value());
}

TEST(Wardrobe, OneAccessoryPerSlotSoASecondReplacesTheFirst)
{
    auto wardrobe = makeWardrobe();
    wardrobe.toggle("hat");
    wardrobe.toggle("beanie");
    EXPECT_EQ(wardrobe.wornIn(Slot::Head), "beanie");
}

TEST(Wardrobe, TogglingTheWornAccessoryTakesItOff)
{
    auto wardrobe = makeWardrobe();
    wardrobe.toggle("hat");
    wardrobe.toggle("hat");
    EXPECT_FALSE(wardrobe.wornIn(Slot::Head).has_value());
}

TEST(Wardrobe, DifferentSlotsDoNotInterfere)
{
    auto wardrobe = makeWardrobe();
    wardrobe.toggle("hat");
    wardrobe.toggle("glasses");
    wardrobe.toggle("bowtie");
    EXPECT_EQ(wardrobe.wornIn(Slot::Head), "hat");
    EXPECT_EQ(wardrobe.wornIn(Slot::Face), "glasses");
    EXPECT_EQ(wardrobe.wornIn(Slot::Body), "bowtie");
}

TEST(Wardrobe, AccessoryIsHiddenInAnActionItDoesNotSupportAndReappears)
{
    auto wardrobe = Wardrobe{{AccessorySpec{"hat", Slot::Head, {Action::Idle, Action::Study}}}};
    wardrobe.toggle("hat");
    EXPECT_EQ(wardrobe.visibleFor(Action::Study), std::vector<std::string>{"hat"});
    EXPECT_TRUE(wardrobe.visibleFor(Action::Sleep).empty());

    // Hidden, not removed: it is still on its slot when the dog wakes.
    EXPECT_EQ(wardrobe.wornIn(Slot::Head), "hat");
    EXPECT_EQ(wardrobe.visibleFor(Action::Idle), std::vector<std::string>{"hat"});
}

TEST(Wardrobe, SleepHidesEveryAccessory)
{
    auto wardrobe = makeWardrobe();
    wardrobe.toggle("hat");
    wardrobe.toggle("glasses");
    wardrobe.toggle("bowtie");
    EXPECT_TRUE(wardrobe.visibleFor(Action::Sleep).empty());
}

TEST(Wardrobe, TogglingAnUnknownAccessoryIsAnError)
{
    auto wardrobe = makeWardrobe();
    EXPECT_THROW(wardrobe.toggle("crown"), std::invalid_argument);
}

} // namespace
