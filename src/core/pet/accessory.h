#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/pet/pet_state.h"

/**
 * @file accessory.h
 * @brief Accessories for the desktop pet: three slots, one accessory per slot, and per-action visibility
 *        (PHASE3 3.4).
 */

namespace lens::core::pet {

/// @brief Where an accessory is worn. Each slot holds at most one accessory at a time.
enum class Slot : std::uint8_t {
    Head,
    Face,
    Body,
};

/// @brief One accessory from accessories.json: its id, the slot it wears in, and the actions it stays visible in.
struct AccessorySpec {
    std::string id;
    Slot slot;
    std::vector<Action> supportedActions;
};

/**
 * @brief The accessories the dog is wearing, one per slot, over a fixed catalogue.
 *
 * Toggling an accessory puts it on its slot, takes off whatever was there, or, if the same one is already
 * worn, takes it off. Visibility is decided per action: an accessory that does not support the action now
 * playing is hidden, not removed, and reappears when the action changes back.
 */
struct Wardrobe {
    /// @param catalogue Every accessory the pet knows. Ids must be unique; the parser enforces that.
    explicit Wardrobe(std::vector<AccessorySpec> catalogue);

    /// @brief Puts the accessory on its slot, takes off the one already there, or takes this one off.
    /// @param id Id of an accessory in the catalogue.
    /// @throws std::invalid_argument If no accessory in the catalogue has this id.
    void toggle(std::string const& id);

    /// @return Id of the accessory worn in `slot`, or nothing.
    std::optional<std::string> wornIn(Slot slot) const;

    /// @return Ids of the worn accessories that support `action`, in slot order.
    std::vector<std::string> visibleFor(Action action) const;

private:
    std::vector<AccessorySpec> catalogue;
    std::map<Slot, std::string> worn;
};

} // namespace lens::core::pet
