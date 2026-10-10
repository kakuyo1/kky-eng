/**
 * @file pet_store.cpp
 * @brief Implementation of PetStore: reads and writes the PET object of the settings document.
 */

#include "app/pet/pet_store.h"

#include <algorithm>
#include <utility>

namespace lens::app::pet {

namespace {

using nlohmann::json;

/// @return Whether `key` in `object` is a boolean true. Absent, null, or any other type reads as false.
bool flagOf(json const* object, char const* key)
{
    return object and object->contains(key) and object->at(key).is_boolean() and object->at(key).get<bool>();
}

/// @return The id stored under `key`, or nothing when the slot is bare.
std::optional<std::string> slotOf(json const& worn, char const* key)
{
    if (not worn.contains(key) or not worn.at(key).is_string()) return std::nullopt;
    return worn.at(key).get<std::string>();
}

/// @return The id as JSON, or null for a bare slot.
json slotJson(std::optional<std::string> const& id)
{
    return id ? json(*id) : json(nullptr);
}

} // namespace

PetStore::PetStore(core::KnownStore& store)
    : store_(store)
{
}

bool PetStore::enabled() const
{
    return flagOf(section(), "enabled");
}

void PetStore::setEnabled(bool on)
{
    write("enabled", on);
}

bool PetStore::passthrough() const
{
    return flagOf(section(), "passthrough");
}

void PetStore::setPassthrough(bool on)
{
    write("passthrough", on);
}

int PetStore::scale(int fallback) const
{
    auto const* pet = section();
    if (not pet or not pet->contains("scale") or not pet->at("scale").is_number_integer()) return fallback;
    return std::clamp(pet->at("scale").get<int>(), kMinScale, kMaxScale);
}

void PetStore::setScale(int times)
{
    write("scale", std::clamp(times, kMinScale, kMaxScale));
}

Outfit PetStore::outfit() const
{
    auto const* pet = section();
    if (not pet or not pet->contains("accessories")) return {};
    auto const& worn = pet->at("accessories");
    return Outfit{slotOf(worn, "head"), slotOf(worn, "face"), slotOf(worn, "body")};
}

void PetStore::setOutfit(Outfit const& outfit)
{
    write("accessories", json{{"head", slotJson(outfit.head)}, {"face", slotJson(outfit.face)}, {"body", slotJson(outfit.body)}});
}

std::optional<SavedPosition> PetStore::position() const
{
    auto const* pet = section();
    if (not pet or not pet->contains("position")) return std::nullopt;
    auto const& at = pet->at("position");
    if (not at.is_object() or not at.contains("screen") or not at.contains("x") or not at.contains("y")) return std::nullopt;
    return SavedPosition{at.at("screen").get<std::string>(), at.at("x").get<int>(), at.at("y").get<int>()};
}

void PetStore::setPosition(SavedPosition const& at)
{
    write("position", json{{"screen", at.screen}, {"x", at.x}, {"y", at.y}});
}

json const* PetStore::section() const
{
    auto const& doc = store_.document();
    return doc.contains("PET") ? &doc.at("PET") : nullptr;
}

void PetStore::write(char const* key, json value)
{
    auto& doc = store_.document();
    if (not doc["PET"].is_object()) doc["PET"] = json::object();
    doc["PET"][key] = std::move(value);
    store_.save();
}

} // namespace lens::app::pet
