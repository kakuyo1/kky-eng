/**
 * @file pet_config_test.cpp
 * @brief The pet data parsers, offline: a document is valid or it is refused whole (PHASE3 3.1, 3.8, 3.9).
 */

#include <stdexcept>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/pet/pet_config.h"

namespace {

using lens::core::pet::Action;
using lens::core::pet::parseAccessories;
using lens::core::pet::parseAnchors;
using lens::core::pet::parseAnimations;
using nlohmann::json;

/// A complete animations document: every required action, stretch, and the blink entry.
json animationsDocument()
{
    return json{{"actions", {
                                {"idle", {{"frames", 5}, {"fps", 5}, {"loop", true}, {"blink", true}}},
                                {"study", {{"frames", 6}, {"fps", 4}, {"loop", true}, {"blink", true}}},
                                {"thinking", {{"frames", 4}, {"fps", 3}, {"loop", true}, {"blink", true}}},
                                {"celebrate", {{"frames", 8}, {"fps", 10}, {"loop", false}}},
                                {"encourage", {{"frames", 5}, {"fps", 8}, {"loop", false}}},
                                {"sleep", {{"frames", 4}, {"fps", 3}, {"loop", true}}},
                                {"click_react", {{"frames", 4}, {"fps", 12}, {"loop", false}}},
                                {"pickup", {{"frames", 4}, {"fps", 6}, {"loop", true}}},
                                {"look_around", {{"frames", 6}, {"fps", 6}, {"loop", false}}},
                                {"yawn", {{"frames", 5}, {"fps", 6}, {"loop", false}, {"returnTo", "sleep"}}},
                                {"stretch", {{"frames", 6}, {"fps", 6}, {"loop", false}}},
                                {"blink", {{"frames", 3}, {"fps", 10}, {"loop", false}}},
                            }}};
}

/// Anchors for every action in an animations document: one zero point per frame.
json anchorsDocument(json const& animations)
{
    json doc = {{"actions", json::object()}};
    for (auto const& [name, entry] : animations.at("actions").items()) {
        if (name == "blink") continue;
        json perFrame = json::array();
        for (int i = 0; i < entry.at("frames").get<int>(); ++i) {
            perFrame.push_back({{"head", {0, 0}}, {"face", {0, 0}}, {"body", {0, 0}}});
        }
        doc["actions"][name] = perFrame;
    }
    return doc;
}

/// The prototype's seven accessories: three slots, sleep hides them all.
json accessoriesDocument()
{
    auto const visible = json::array({"idle", "study", "thinking", "celebrate", "encourage", "click_react", "pickup", "look_around", "yawn", "stretch"});
    return json{{"accessories", {
                                    {{"id", "hat"}, {"slot", "head"}, {"supportedActions", visible}},
                                    {{"id", "beanie"}, {"slot", "head"}, {"supportedActions", visible}},
                                    {{"id", "sprout"}, {"slot", "head"}, {"supportedActions", visible}},
                                    {{"id", "crown"}, {"slot", "head"}, {"supportedActions", visible}},
                                    {{"id", "glasses"}, {"slot", "face"}, {"supportedActions", visible}},
                                    {{"id", "sunglasses"}, {"slot", "face"}, {"supportedActions", visible}},
                                    {{"id", "bowtie"}, {"slot", "body"}, {"supportedActions", visible}},
                                }}};
}

TEST(PetConfig, ParsesEveryActionWithItsPlayback)
{
    auto const animations = parseAnimations(animationsDocument());
    ASSERT_EQ(animations.actions.size(), 11u);
    EXPECT_EQ(animations.actions.at(Action::Study).frames, 6);
    EXPECT_EQ(animations.actions.at(Action::Study).fps, 4);
    EXPECT_TRUE(animations.actions.at(Action::Study).loop);
    EXPECT_FALSE(animations.actions.at(Action::Celebrate).returnTo.has_value());
}

TEST(PetConfig, YawnReturnsToSleep)
{
    auto const animations = parseAnimations(animationsDocument());
    ASSERT_TRUE(animations.actions.at(Action::Yawn).returnTo.has_value());
    EXPECT_EQ(*animations.actions.at(Action::Yawn).returnTo, Action::Sleep);
}

TEST(PetConfig, BlinkIsItsOwnEntryNotAnAction)
{
    auto const animations = parseAnimations(animationsDocument());
    EXPECT_EQ(animations.blink.frames, 3);
    EXPECT_EQ(animations.blink.fps, 10);
    EXPECT_FALSE(animations.blink.loop);
}

TEST(PetConfig, BlinkFlagIsReadPerAction)
{
    auto const animations = parseAnimations(animationsDocument());
    EXPECT_TRUE(animations.actions.at(Action::Idle).blinks);
    EXPECT_TRUE(animations.actions.at(Action::Study).blinks);
    EXPECT_FALSE(animations.actions.at(Action::Sleep).blinks);
}

TEST(PetConfig, StretchMayBeLeftOut)
{
    // PHASE3 3.1: stretch is not required in the first version, so a document without it still loads.
    auto doc = animationsDocument();
    doc["actions"].erase("stretch");
    auto const animations = parseAnimations(doc);
    EXPECT_EQ(animations.actions.count(Action::Stretch), 0u);
}

TEST(PetConfig, RefusesADocumentMissingARequiredAction)
{
    auto doc = animationsDocument();
    doc["actions"].erase("sleep");
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesADocumentWithoutTheBlinkEntry)
{
    auto doc = animationsDocument();
    doc["actions"].erase("blink");
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesAFrameCountBelowOne)
{
    auto doc                         = animationsDocument();
    doc["actions"]["idle"]["frames"] = 0;
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesANonPositiveFrameRate)
{
    auto doc                       = animationsDocument();
    doc["actions"]["study"]["fps"] = 0;
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesAnActionNameTheMachineDoesNotKnow)
{
    auto doc                = animationsDocument();
    doc["actions"]["dance"] = {{"frames", 4}, {"fps", 4}, {"loop", true}};
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesAReturnToTheMachineDoesNotKnow)
{
    auto doc                           = animationsDocument();
    doc["actions"]["yawn"]["returnTo"] = "dance";
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesAReturnToAnActionWithNoPlayback)
{
    // Yawn returns to stretch, but the document has no stretch: the dog would have nothing to play.
    auto doc                           = animationsDocument();
    doc["actions"]["yawn"]["returnTo"] = "stretch";
    doc["actions"].erase("stretch");
    EXPECT_THROW(parseAnimations(doc), std::runtime_error);
}

TEST(PetConfig, RefusesAMissingActionsObject)
{
    EXPECT_THROW(parseAnimations(json::object()), std::runtime_error);
}

TEST(PetConfig, ParsesAnchorsForEveryFrame)
{
    auto const animations             = parseAnimations(animationsDocument());
    auto doc                          = anchorsDocument(animationsDocument());
    doc["actions"]["idle"][2]["head"] = {0, -3};
    auto const anchors                = parseAnchors(doc, animations.actions);
    ASSERT_EQ(anchors.at(Action::Idle).size(), 5u);
    EXPECT_EQ(anchors.at(Action::Idle)[2].head.y, -3);
}

TEST(PetConfig, RefusesAnActionWithoutAnchors)
{
    auto const animations = parseAnimations(animationsDocument());
    auto doc              = anchorsDocument(animationsDocument());
    doc["actions"].erase("idle");
    EXPECT_THROW(parseAnchors(doc, animations.actions), std::runtime_error);
}

TEST(PetConfig, RefusesAnchorsThatDoNotMatchTheFrameCount)
{
    auto const animations = parseAnimations(animationsDocument());
    auto doc              = anchorsDocument(animationsDocument());
    doc["actions"]["idle"].push_back({{"head", {0, 0}}, {"face", {0, 0}}, {"body", {0, 0}}});
    EXPECT_THROW(parseAnchors(doc, animations.actions), std::runtime_error);
}

TEST(PetConfig, RefusesAnAnchorPointThatIsMissing)
{
    auto const animations = parseAnimations(animationsDocument());
    auto doc              = anchorsDocument(animationsDocument());
    doc["actions"]["study"][3].erase("face");
    EXPECT_THROW(parseAnchors(doc, animations.actions), std::runtime_error);
}

TEST(PetConfig, RefusesAnchorsForAnActionTheDocumentDoesNotDefine)
{
    auto const animations   = parseAnimations(animationsDocument());
    auto doc                = anchorsDocument(animationsDocument());
    doc["actions"]["dance"] = json::array({{{"head", {0, 0}}, {"face", {0, 0}}, {"body", {0, 0}}}});
    EXPECT_THROW(parseAnchors(doc, animations.actions), std::runtime_error);
}

TEST(PetConfig, ParsesTheAccessoryCatalogue)
{
    auto const catalogue = parseAccessories(accessoriesDocument());
    ASSERT_EQ(catalogue.size(), 7u);
    EXPECT_EQ(catalogue.front().id, "hat");
    EXPECT_EQ(catalogue.back().id, "bowtie");
}

TEST(PetConfig, RefusesAnAccessoryInAnUnknownSlot)
{
    auto doc                      = accessoriesDocument();
    doc["accessories"][0]["slot"] = "hand";
    EXPECT_THROW(parseAccessories(doc), std::runtime_error);
}

TEST(PetConfig, RefusesARepeatedAccessoryId)
{
    auto doc                    = accessoriesDocument();
    doc["accessories"][1]["id"] = "hat";
    EXPECT_THROW(parseAccessories(doc), std::runtime_error);
}

TEST(PetConfig, RefusesAccessoryVisibleInAnUnknownAction)
{
    auto doc                                     = accessoriesDocument();
    doc["accessories"][0]["supportedActions"][0] = "dance";
    EXPECT_THROW(parseAccessories(doc), std::runtime_error);
}

} // namespace
