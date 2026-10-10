/**
 * @file accessory.cpp
 * @brief Implementation of Wardrobe: the slot rule and per-action visibility.
 */

#include "core/pet/accessory.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace lens::core::pet {

Wardrobe::Wardrobe(std::vector<AccessorySpec> catalogue_)
    : catalogue{std::move(catalogue_)}
{
}

void Wardrobe::toggle(std::string const& id)
{
    auto const found = std::find_if(catalogue.begin(), catalogue.end(), [&id](AccessorySpec const& spec) { return spec.id == id; });
    if (found == catalogue.end()) throw std::invalid_argument("accessory: unknown id");

    auto const current = worn.find(found->slot);
    if (current != worn.end() and current->second == id) {
        worn.erase(current);
        return;
    }
    worn[found->slot] = id;
}

std::optional<std::string> Wardrobe::wornIn(Slot slot) const
{
    auto const current = worn.find(slot);
    if (current == worn.end()) return std::nullopt;
    return current->second;
}

std::vector<std::string> Wardrobe::visibleFor(Action action) const
{
    std::vector<std::string> visible;
    for (auto const& wearing : worn) {
        auto const& id  = wearing.second;
        auto const spec = std::find_if(catalogue.begin(), catalogue.end(), [&id](AccessorySpec const& candidate) { return candidate.id == id; });
        if (spec == catalogue.end()) continue;
        if (std::find(spec->supportedActions.begin(), spec->supportedActions.end(), action) != spec->supportedActions.end()) {
            visible.push_back(id);
        }
    }
    return visible;
}

} // namespace lens::core::pet
