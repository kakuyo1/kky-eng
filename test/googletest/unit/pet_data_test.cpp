/**
 * @file pet_data_test.cpp
 * @brief The shipped pet data, offline: data/pet/*.json must pass the parsers the app runs at start (PHASE3 3.8).
 */

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "app/pet/pet_assets.h"
#include "core/pet/pet_config.h"

namespace {

using lens::core::pet::parseAccessories;
using lens::core::pet::parseAnchors;
using lens::core::pet::parseAnimations;
using nlohmann::json;

json load(char const* name)
{
    auto const path = std::filesystem::path{LENS_SOURCE_DIR} / "data" / "pet" / name;
    auto stream     = std::ifstream{path};
    EXPECT_TRUE(stream.is_open()) << path.string();
    return json::parse(stream);
}

TEST(PetData, AnimationsLoadAndCarryTheBlink)
{
    auto const animations = parseAnimations(load("animations.json"));

    EXPECT_EQ(animations.actions.size(), 11u);
    EXPECT_EQ(animations.blink.frames, 3);
    EXPECT_FALSE(animations.blink.loop);
}

TEST(PetData, AnchorsMatchTheFrameCountOfEveryAction)
{
    auto const animations = parseAnimations(load("animations.json"));

    EXPECT_NO_THROW(parseAnchors(load("anchors.json"), animations.actions));
}

TEST(PetData, ExpressionListsCoverEveryFrame)
{
    auto const doc = load("animations.json");

    for (auto const& [name, entry] : doc.at("actions").items()) {
        if (not entry.contains("expressions")) continue;
        EXPECT_EQ(entry.at("expressions").size(), entry.at("frames").get<std::size_t>()) << name;
    }
}

TEST(PetData, AccessoriesLoadWithEveryPrototypeItem)
{
    auto const catalogue = parseAccessories(load("accessories.json"));

    EXPECT_EQ(catalogue.size(), 7u);
}

TEST(PetData, EverySheetIsTheSizeItsFrameCountSays)
{
    auto const directory = std::filesystem::path{LENS_SOURCE_DIR} / "data" / "pet";

    EXPECT_NO_THROW(lens::app::pet::PetAssets::load(directory));
}

TEST(PetData, LayersStackInTheSpecOrder)
{
    // PHASE3 3.7: body, expression, body accessories, face, head accessories, effect.
    auto const layers      = load("pet.json").at("layers");
    auto const accessories = load("accessories.json").at("accessories");
    auto const zOf         = [&accessories](char const* slot) {
        auto zs = std::vector<int>{};
        for (auto const& item : accessories) {
            if (item.at("slot") == slot) zs.push_back(item.at("zIndex").get<int>());
        }
        return zs;
    };

    auto const body       = layers.at("body").get<int>();
    auto const expression = layers.at("expression").get<int>();
    auto const effect     = layers.at("effect").get<int>();
    auto const bodyWorn   = zOf("body");
    auto const faceWorn   = zOf("face");
    auto const headWorn   = zOf("head");

    ASSERT_FALSE(bodyWorn.empty());
    ASSERT_FALSE(faceWorn.empty());
    ASSERT_FALSE(headWorn.empty());
    EXPECT_LT(body, expression);
    EXPECT_LT(expression, *std::min_element(bodyWorn.begin(), bodyWorn.end()));
    EXPECT_LT(*std::max_element(bodyWorn.begin(), bodyWorn.end()), *std::min_element(faceWorn.begin(), faceWorn.end()));
    EXPECT_LT(*std::max_element(faceWorn.begin(), faceWorn.end()), *std::min_element(headWorn.begin(), headWorn.end()));
    EXPECT_LT(*std::max_element(headWorn.begin(), headWorn.end()), effect);
}

} // namespace
