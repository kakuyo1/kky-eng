#pragma once

#include <nlohmann/json.hpp>

#include "core/pet/pet_state.h"

/**
 * @file pet_config.h
 * @brief Reads the action table of the desktop pet's data (PHASE3 3.1, 3.8).
 *
 * Refusal is all or nothing: a document with a missing required action, a playback that cannot run,
 * or an action name the machine does not know is rejected whole, and the pet does not start with it.
 */

namespace lens::core::pet {

/**
 * @brief Parses the `actions` object of data/pet/animations.json into playback for the state machine.
 *
 * Every action but stretch is required (PHASE3 3.1). `frames` and `fps` must be at least 1, `loop` must be a
 * boolean, and `returnTo` is optional and must name a known action.
 *
 * @param document The whole parsed animations.json.
 * @return Playback for every action the document defines.
 * @throws std::runtime_error If the document is refused, with the reason in the message.
 */
ActionSpecs parseAnimations(nlohmann::json const& document);

} // namespace lens::core::pet
