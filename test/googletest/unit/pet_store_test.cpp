/**
 * @file pet_store_test.cpp
 * @brief The PET section's size setting, offline: the slider's stops are whole numbers and out-of-range values clamp (PHASE3 3.6).
 */

#include <filesystem>

#include <gtest/gtest.h>

#include "app/pet/pet_store.h"
#include "core/known_store.h"

namespace {

using lens::app::pet::kMaxScale;
using lens::app::pet::kMinScale;
using lens::app::pet::PetStore;
using lens::core::KnownStore;

TEST(PetStore, ScaleFallsBackUntilTheReaderChoosesOne)
{
    auto const path = std::filesystem::temp_directory_path() / "lens-pet-store-fallback.json";
    auto store      = KnownStore::load(path);
    auto pet        = PetStore{store};

    EXPECT_EQ(pet.scale(3), 3);
}

TEST(PetStore, OutfitReadsBackAsItWasWritten)
{
    auto const path = std::filesystem::temp_directory_path() / "lens-pet-store-outfit.json";
    auto store      = KnownStore::load(path);
    auto pet        = PetStore{store};

    pet.setOutfit(lens::app::pet::Outfit{.head = std::string{"hat"}, .face = std::nullopt, .body = std::nullopt});

    auto const worn = pet.outfit();
    EXPECT_EQ(worn.head, std::optional<std::string>{"hat"});
    EXPECT_FALSE(worn.face);
    EXPECT_FALSE(worn.body);
}

TEST(PetStore, ScaleClampsToTheSliderRange)
{
    auto const path = std::filesystem::temp_directory_path() / "lens-pet-store-clamp.json";
    auto store      = KnownStore::load(path);
    auto pet        = PetStore{store};

    pet.setScale(9);
    EXPECT_EQ(pet.scale(3), kMaxScale);

    pet.setScale(0);
    EXPECT_EQ(pet.scale(3), kMinScale);

    pet.setScale(2);
    EXPECT_EQ(pet.scale(3), 2);
}

} // namespace
