#include "core/pet/pet_config.h"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace lens::core::pet {

namespace {

struct NamedAction {
    std::string_view name;
    Action action;
};

/// Names as they appear in animations.json. The machine has no other spelling of an action.
constexpr std::array<NamedAction, 11> kNamedActions{{
    {"idle", Action::Idle},
    {"study", Action::Study},
    {"thinking", Action::Thinking},
    {"celebrate", Action::Celebrate},
    {"encourage", Action::Encourage},
    {"sleep", Action::Sleep},
    {"click_react", Action::ClickReact},
    {"pickup", Action::Pickup},
    {"look_around", Action::LookAround},
    {"yawn", Action::Yawn},
    {"stretch", Action::Stretch},
}};

/// Every action except stretch, which the first version may leave out (PHASE3 3.1).
constexpr std::array<Action, 10> kRequiredActions{{
    Action::Idle,
    Action::Study,
    Action::Thinking,
    Action::Celebrate,
    Action::Encourage,
    Action::Sleep,
    Action::ClickReact,
    Action::Pickup,
    Action::LookAround,
    Action::Yawn,
}};

std::optional<Action> actionNamed(std::string const& name)
{
    for (auto const& entry : kNamedActions) {
        if (entry.name == name) return entry.action;
    }
    return std::nullopt;
}

int positiveInt(nlohmann::json const& entry, char const* key, std::string const& action)
{
    if (not entry.contains(key) or not entry.at(key).is_number_integer())
        throw std::runtime_error("animations: " + action + "." + key + " must be an integer");
    auto const value = entry.at(key).get<int>();
    if (value < 1) throw std::runtime_error("animations: " + action + "." + key + " must be at least 1");
    return value;
}

} // namespace

ActionSpecs parseAnimations(nlohmann::json const& document)
{
    if (not document.is_object() or not document.contains("actions") or not document.at("actions").is_object())
        throw std::runtime_error("animations: missing actions object");

    ActionSpecs specs;
    for (auto const& [name, entry] : document.at("actions").items()) {
        auto const action = actionNamed(name);
        if (not action) throw std::runtime_error("animations: unknown action " + name);
        if (not entry.is_object()) throw std::runtime_error("animations: " + name + " must be an object");

        if (not entry.contains("loop") or not entry.at("loop").is_boolean())
            throw std::runtime_error("animations: " + name + ".loop must be a boolean");

        ActionSpec spec{positiveInt(entry, "frames", name), positiveInt(entry, "fps", name), entry.at("loop").get<bool>(), std::nullopt};
        if (entry.contains("returnTo")) {
            if (not entry.at("returnTo").is_string())
                throw std::runtime_error("animations: " + name + ".returnTo must be a string");
            auto const target = actionNamed(entry.at("returnTo").get<std::string>());
            if (not target) throw std::runtime_error("animations: " + name + ".returnTo names an unknown action");
            spec.returnTo = target;
        }
        specs.emplace(*action, spec);
    }

    for (auto const required : kRequiredActions) {
        if (specs.count(required) != 0) continue;
        for (auto const& entry : kNamedActions) {
            if (entry.action == required) throw std::runtime_error("animations: required action missing: " + std::string{entry.name});
        }
    }
    return specs;
}

} // namespace lens::core::pet
