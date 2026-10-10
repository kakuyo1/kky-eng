#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <QString>

#include "core/pet/accessory.h"
#include "core/pet/pet_config.h"

/**
 * @file pet_assets.h
 * @brief The desktop pet's data directory, read once at start-up and checked against its sprite sheets (PHASE3 3.8).
 *
 * The core parsers own animations, anchors and the accessory catalogue. This file adds what only the window needs:
 * sheet paths, expression indices, layer order, and each accessory's picture. Everything is checked before the
 * pet starts, so a missing or wrong-sized sheet refuses the whole pet and never a half-drawn one.
 */

namespace lens::app::pet {

/// @brief Layer order from pet.json. A larger z is drawn on top.
struct Layers {
    int body;
    int expression;
    int effect;
};

/// @brief Where one action's pictures are, and which expression each frame wears.
struct ActionSheets {
    /// Absolute path of the body sheet: one frame per cell, left to right.
    QString body;
    /// Absolute path of the effect sheet; empty when the action has no effect layer.
    QString effect;
    /// Expression sheet index for each frame; empty means expression 0 throughout.
    std::vector<int> expressions;
};

/// @brief One accessory's picture and stacking, from accessories.json.
struct AccessoryArt {
    QString asset;
    int zIndex;
};

/// @brief Everything the pet draws with, parsed and verified.
struct PetAssets {
    /// Side of one square frame, in sheet units.
    int canvas;
    /// Whole-number magnification on screen.
    int scale;
    Layers layers;
    QString expressionSheet;
    core::pet::Animations animations;
    core::pet::AnchorTable anchors;
    std::vector<core::pet::AccessorySpec> catalogue;
    std::map<core::pet::Action, ActionSheets> sheets;
    std::map<std::string, AccessoryArt> accessories;
    /// Expression indices the blink plays through, one per blink frame.
    std::vector<int> blinkExpressions;

    /**
     * @brief Reads every file under `directory` and checks each sheet's size against its frame count.
     * @param directory The data/pet folder. Sheet paths in the documents are relative to it.
     * @return The whole pet.
     * @throws std::runtime_error If a sheet is missing or the wrong size, or a document is refused.
     * @throws std::exception (nlohmann::json) If a file is not valid JSON or lacks a field.
     */
    static PetAssets load(std::filesystem::path const& directory);
};

/// @return The data-file name of `action`, as the state machine and the QML side both spell it.
std::string actionName(core::pet::Action action);

} // namespace lens::app::pet
