/**
 * @file pet_config.cpp
 * @brief Implementation of the pet data parsers: animations, anchors and accessories.
 */

#include "core/pet/pet_config.h"

#include <array>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace lens::core::pet {

namespace {

using nlohmann::json;

struct NamedAction {
    std::string_view name;
    Action action;
    bool required;
};

/// Names as they appear in the data files. Stretch is the only action the first version may leave out (PHASE3 3.1).
constexpr std::array<NamedAction, 11> kNamedActions{{
    {"idle", Action::Idle, true},
    {"study", Action::Study, true},
    {"thinking", Action::Thinking, true},
    {"celebrate", Action::Celebrate, true},
    {"encourage", Action::Encourage, true},
    {"sleep", Action::Sleep, true},
    {"click_react", Action::ClickReact, true},
    {"pickup", Action::Pickup, true},
    {"look_around", Action::LookAround, true},
    {"yawn", Action::Yawn, true},
    {"stretch", Action::Stretch, false},
}};

/// @return The action with this name in the data files, or nothing.
std::optional<Action> actionNamed(std::string const& name)
{
    for (auto const& entry : kNamedActions) {
        if (entry.name == name) return entry.action;
    }
    return std::nullopt;
}

/// @return The data-file name of `action`. Every action the machine knows has one.
std::string nameOf(Action action)
{
    for (auto const& entry : kNamedActions) {
        if (entry.action == action) return std::string{entry.name};
    }
    return {};
}

/// @return The slot with this name in accessories.json, or nothing.
std::optional<Slot> slotNamed(std::string const& name)
{
    if (name == "head") return Slot::Head;
    if (name == "face") return Slot::Face;
    if (name == "body") return Slot::Body;
    return std::nullopt;
}

/// @return `entry[key]` as an integer of at least 1. Refuses anything else, naming the owner in the message.
int positiveInt(json const& entry, char const* key, std::string const& owner)
{
    if (not entry.contains(key) or not entry.at(key).is_number_integer())
        throw std::runtime_error("animations: " + owner + "." + key + " must be an integer");
    auto const value = entry.at(key).get<int>();
    if (value < 1) throw std::runtime_error("animations: " + owner + "." + key + " must be at least 1");
    return value;
}

/// One playback entry. `returnTo` is resolved to an action here, and checked against the document later.
ActionSpec parseSpec(json const& entry, std::string const& name)
{
    if (not entry.is_object()) throw std::runtime_error("animations: " + name + " must be an object");
    if (not entry.contains("loop") or not entry.at("loop").is_boolean())
        throw std::runtime_error("animations: " + name + ".loop must be a boolean");

    ActionSpec spec{
        .frames   = positiveInt(entry, "frames", name),
        .fps      = positiveInt(entry, "fps", name),
        .loop     = entry.at("loop").get<bool>(),
        .returnTo = std::nullopt,
        .blinks   = false,
    };
    if (entry.contains("returnTo")) {
        if (not entry.at("returnTo").is_string())
            throw std::runtime_error("animations: " + name + ".returnTo must be a string");
        auto const target = actionNamed(entry.at("returnTo").get<std::string>());
        if (not target) throw std::runtime_error("animations: " + name + ".returnTo names an unknown action");
        spec.returnTo = target;
    }
    if (entry.contains("blink")) {
        if (not entry.at("blink").is_boolean()) throw std::runtime_error("animations: " + name + ".blink must be a boolean");
        spec.blinks = entry.at("blink").get<bool>();
    }
    return spec;
}

/// @return An anchor point from a `[x, y]` pair of integers. Refuses any other shape.
Point parsePoint(json const& value, std::string const& owner)
{
    if (not value.is_array() or value.size() != 2 or not value.at(0).is_number_integer() or
        not value.at(1).is_number_integer())
        throw std::runtime_error("anchors: " + owner + " must be a pair of integers");
    return Point{value.at(0).get<int>(), value.at(1).get<int>()};
}

/// @return The anchor point stored under `key` in one frame's entry. Refuses it if the key is absent.
Point pointOf(json const& entry, char const* key, std::string const& owner)
{
    if (not entry.contains(key)) throw std::runtime_error("anchors: " + owner + " is missing " + key);
    return parsePoint(entry.at(key), owner + "." + key);
}

} // namespace

Animations parseAnimations(json const& document)
{
    if (not document.is_object() or not document.contains("actions") or not document.at("actions").is_object())
        throw std::runtime_error("animations: missing actions object");

    Animations animations{};
    bool hasBlink = false;
    for (auto const& [name, entry] : document.at("actions").items()) {
        if (name == "blink") {
            animations.blink = parseSpec(entry, name);
            hasBlink         = true;
            continue;
        }
        auto const action = actionNamed(name);
        if (not action) throw std::runtime_error("animations: unknown action " + name);
        animations.actions.emplace(*action, parseSpec(entry, name));
    }

    if (not hasBlink) throw std::runtime_error("animations: blink entry missing");
    for (auto const& named : kNamedActions) {
        if (named.required and animations.actions.count(named.action) == 0)
            throw std::runtime_error("animations: required action missing: " + std::string{named.name});
    }
    for (auto const& [action, spec] : animations.actions) {
        if (spec.returnTo and animations.actions.count(*spec.returnTo) == 0)
            throw std::runtime_error("animations: " + nameOf(action) + ".returnTo has no playback in the document");
    }
    return animations;
}

AnchorTable parseAnchors(json const& document, ActionSpecs const& actions)
{
    if (not document.is_object() or not document.contains("actions") or not document.at("actions").is_object())
        throw std::runtime_error("anchors: missing actions object");

    AnchorTable table;
    for (auto const& [name, entries] : document.at("actions").items()) {
        auto const action = actionNamed(name);
        if (not action or actions.count(*action) == 0) throw std::runtime_error("anchors: unknown action " + name);
        if (not entries.is_array()) throw std::runtime_error("anchors: " + name + " must be an array");

        auto const frames = actions.at(*action).frames;
        if (static_cast<int>(entries.size()) != frames)
            throw std::runtime_error("anchors: " + name + " has " + std::to_string(entries.size()) + " entries for " +
                                     std::to_string(frames) + " frames");

        std::vector<Anchors> perFrame;
        for (auto const& entry : entries) {
            if (not entry.is_object()) throw std::runtime_error("anchors: " + name + " entries must be objects");
            perFrame.push_back(Anchors{
                .head = pointOf(entry, "head", name),
                .face = pointOf(entry, "face", name),
                .body = pointOf(entry, "body", name),
            });
        }
        table.emplace(*action, std::move(perFrame));
    }

    for (auto const& playable : actions) {
        if (table.count(playable.first) == 0) throw std::runtime_error("anchors: missing for " + nameOf(playable.first));
    }
    return table;
}

std::vector<AccessorySpec> parseAccessories(json const& document)
{
    if (not document.is_object() or not document.contains("accessories") or not document.at("accessories").is_array())
        throw std::runtime_error("accessories: missing accessories array");

    std::vector<AccessorySpec> catalogue;
    std::set<std::string> ids;
    for (auto const& entry : document.at("accessories")) {
        if (not entry.is_object() or not entry.contains("id") or not entry.at("id").is_string())
            throw std::runtime_error("accessories: each accessory needs a string id");
        auto id = entry.at("id").get<std::string>();
        if (not ids.insert(id).second) throw std::runtime_error("accessories: duplicate id " + id);

        if (not entry.contains("slot") or not entry.at("slot").is_string())
            throw std::runtime_error("accessories: " + id + " needs a slot");
        auto const slot = slotNamed(entry.at("slot").get<std::string>());
        if (not slot) throw std::runtime_error("accessories: " + id + " names an unknown slot");

        if (not entry.contains("supportedActions") or not entry.at("supportedActions").is_array())
            throw std::runtime_error("accessories: " + id + " needs a supportedActions array");
        std::vector<Action> supported;
        for (auto const& name : entry.at("supportedActions")) {
            if (not name.is_string()) throw std::runtime_error("accessories: " + id + ".supportedActions must be strings");
            auto const action = actionNamed(name.get<std::string>());
            if (not action) throw std::runtime_error("accessories: " + id + " supports an unknown action");
            supported.push_back(*action);
        }
        catalogue.push_back(AccessorySpec{std::move(id), *slot, std::move(supported)});
    }
    return catalogue;
}

} // namespace lens::core::pet
