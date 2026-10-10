#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "core/known_store.h"

/**
 * @file pet_store.h
 * @brief The PET section of the settings document: the two switches, the worn accessories and the window position
 *        (PHASE3 3.6).
 *
 * Like StatsStore, this binds to the document KnownStore owns and writes only its own key. It persists through
 * KnownStore::save(), so there is still one file and one writer.
 */

namespace lens::app::pet {

/// @brief The accessory worn in each slot, by id. An empty slot is nothing.
struct Outfit {
    std::optional<std::string> head;
    std::optional<std::string> face;
    std::optional<std::string> body;
};

/// @brief The smallest and largest magnification the size slider offers. Whole numbers only: pixels stay square (PHASE3 3.1).
constexpr int kMinScale = 1;
constexpr int kMaxScale = 4;

/// @brief Where the window was left: the screen it sat on, by device name, and its offset from that screen's origin.
struct SavedPosition {
    std::string screen;
    int x;
    int y;
};

class PetStore final {
public:
    /// @param store The one owner of the settings document, which must outlive this object.
    explicit PetStore(core::KnownStore& store);

    /// @return Whether the reader turned the desktop pet on. Off by default.
    bool enabled() const;
    void setEnabled(bool on);

    /// @return Whether the window lets mouse events through. Off by default.
    bool passthrough() const;
    void setPassthrough(bool on);

    /// @param fallback Returned while the reader has not chosen a size, normally the default in pet.json.
    /// @return The magnification in kMinScale..kMaxScale.
    int scale(int fallback) const;
    /// @brief Stores the magnification, clamped to kMinScale..kMaxScale.
    void setScale(int times);

    /// @return The worn accessories, as last saved.
    Outfit outfit() const;
    void setOutfit(Outfit const& outfit);

    /// @return Where the window was left, or nothing before it was first moved.
    std::optional<SavedPosition> position() const;
    void setPosition(SavedPosition const& at);

private:
    /// @return The PET object, or nothing before the first write.
    nlohmann::json const* section() const;
    /// @brief Writes one key of the PET object and saves the document.
    void write(char const* key, nlohmann::json value);

    core::KnownStore& store_;
};

} // namespace lens::app::pet
