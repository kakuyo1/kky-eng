/**
 * @file pet_config_test.cpp
 * @brief parseAnimations, offline: a document is valid or it is refused whole (PHASE3 3.8, 3.9).
 */

#include <stdexcept>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/pet/pet_config.h"

namespace {

using lens::core::pet::Action;
using lens::core::pet::parseAnimations;
using nlohmann::json;

/// A complete document: every required action, plus stretch.
json completeDocument()
{
    return json{{"actions", {
                                {"idle", {{"frames", 5}, {"fps", 5}, {"loop", true}}},
                                {"study", {{"frames", 6}, {"fps", 4}, {"loop", true}}},
                                {"thinking", {{"frames", 4}, {"fps", 3}, {"loop", true}}},
                                {"celebrate", {{"frames", 8}, {"fps", 10}, {"loop", false}}},
                                {"encourage", {{"frames", 5}, {"fps", 8}, {"loop", false}}},
                                {"sleep", {{"frames", 4}, {"fps", 3}, {"loop", true}}},
                                {"click_react", {{"frames", 4}, {"fps", 12}, {"loop", false}}},
                                {"pickup", {{"frames", 4}, {"fps", 6}, {"loop", true}}},
                                {"look_around", {{"frames", 6}, {"fps", 6}, {"loop", false}}},
                                {"yawn", {{"frames", 5}, {"fps", 6}, {"loop", false}, {"returnTo", "sleep"}}},
                                {"stretch", {{"frames", 6}, {"fps", 6}, {"loop", false}}},
                            }}};
}

TEST(PetConfig, ParsesEveryActionWithItsPlayback)
{
    auto const specs = parseAnimations(completeDocument());
    ASSERT_EQ(specs.size(), 11u);
    EXPECT_EQ(specs.at(Action::Study).frames, 6);
    EXPECT_EQ(specs.at(Action::Study).fps, 4);
    EXPECT_TRUE(specs.at(Action::Study).loop);
    EXPECT_FALSE(specs.at(Action::Celebrate).returnTo.has_value());
}

TEST(PetConfig, YawnReturnsToSleep)
{
    auto const specs = parseAnimations(completeDocument());
    ASSERT_TRUE(specs.at(Action::Yawn).returnTo.has_value());
    EXPECT_EQ(*specs.at(Action::Yawn).returnTo, Action::Sleep);
}

TEST(PetConfig, StretchMayBeLeftOut)
{
    // PHASE3 3.1: stretch is not required in the first version, so a document without it still loads.
    auto doc = completeDocument();
    doc["actions"].erase("stretch");
    auto const specs = parseAnimations(doc);
    EXPECT_EQ(specs.count(Action::Stretch), 0u);
}

TEST(PetConfig, RefusesADocumentMissingARequiredAction)
{
    auto doc = completeDocument();
    doc["actions"].erase("sleep");
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesAFrameCountBelowOne)
{
    auto doc                         = completeDocument();
    doc["actions"]["idle"]["frames"] = 0;
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesANonPositiveFrameRate)
{
    auto doc                       = completeDocument();
    doc["actions"]["study"]["fps"] = 0;
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesAnActionNameTheMachineDoesNotKnow)
{
    auto doc                = completeDocument();
    doc["actions"]["dance"] = {{"frames", 4}, {"fps", 4}, {"loop", true}};
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesAReturnToTheMachineDoesNotKnow)
{
    auto doc                           = completeDocument();
    doc["actions"]["yawn"]["returnTo"] = "dance";
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesAMissingActionsObject)
{
    EXPECT_THROW(parseAnimations(json::object()), std::runtime_error);
}

} // namespace
