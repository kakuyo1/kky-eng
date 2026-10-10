#include "core/pet/pet_state.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace lens::core::pet {

namespace {

/// Actions the idle timer picks from (PHASE3 3.3: random idle chooses among the situational actions).
constexpr std::array<Action, 2> kRandomIdle{Action::LookAround, Action::Stretch};

} // namespace

PetStateMachine::PetStateMachine(ActionSpecs specs_, RandomSource random_)
    : specs{std::move(specs_)}, random{std::move(random_)}
{
    idleWait = nextIdleWait();
}

void PetStateMachine::handle(PetEvent event)
{
    resume.reset();
    quiet = std::chrono::milliseconds{0};
    switch (event) {
        case PetEvent::DragStart:
            if (dragging) return;
            dragging = true;
            current  = Action::Pickup;
            priority = Priority::Drag;
            elapsed  = std::chrono::milliseconds{0};
            return;
        case PetEvent::DragEnd:
            if (not dragging) return;
            dragging = false;
            enterIdle();
            return;
        case PetEvent::Click:
            if (not dragging) request(Action::ClickReact, Priority::Click);
            return;
        case PetEvent::SelectionShown:
            request(Action::LookAround, Priority::Event);
            return;
        case PetEvent::ExplanationRequested:
            request(Action::Thinking, Priority::Event);
            return;
        case PetEvent::ExplanationShown:
            held = Action::Study;
            request(Action::Study, Priority::Event);
            return;
        case PetEvent::ExplanationHidden:
            if (held == Action::Study) held.reset();
            if (current == Action::Study) settle();
            return;
        case PetEvent::KnownMarked:
        case PetEvent::UpdateAvailable:
            request(Action::Celebrate, Priority::Event);
            return;
        case PetEvent::NewWordMarked:
            request(Action::Encourage, Priority::Event);
            return;
        case PetEvent::BudgetPaused:
            held = Action::Sleep;
            request(Action::Sleep, Priority::Event);
            return;
        case PetEvent::BudgetResumed:
            if (held == Action::Sleep) held.reset();
            if (current == Action::Sleep) settle();
            return;
    }
}

void PetStateMachine::advance(std::chrono::milliseconds step)
{
    quiet += step;
    if (quiet >= kQuietBeforeYawn) {
        quiet = std::chrono::milliseconds{0};
        request(Action::Yawn, Priority::Situational);
        return;
    }
    if (current == Action::Idle) {
        idle += step;
        if (idle >= idleWait) request(pickRandomIdle(), Priority::Situational);
        return;
    }
    elapsed += step;
    if (not specs.at(current).loop and elapsed >= durationOf(current)) settle();
}

bool PetStateMachine::request(Action next, Priority requested)
{
    // Thinking waits for the explanation that the event completes, so any event replaces it.
    auto const givesWayToThinking = current == Action::Thinking and requested == Priority::Event;
    auto const atOrBelow          = static_cast<std::uint8_t>(requested) <= static_cast<std::uint8_t>(priority);
    if (atOrBelow and not givesWayToThinking) return false;
    current  = next;
    priority = requested;
    resume   = specs.at(next).returnTo;
    elapsed  = std::chrono::milliseconds{0};
    return true;
}

void PetStateMachine::enterIdle()
{
    current  = Action::Idle;
    priority = Priority::Idle;
    elapsed  = std::chrono::milliseconds{0};
    idle     = std::chrono::milliseconds{0};
    idleWait = nextIdleWait();
}

void PetStateMachine::settle()
{
    auto const next = resume.value_or(held.value_or(Action::Idle));
    resume.reset();
    if (next == Action::Idle) {
        enterIdle();
        return;
    }
    current  = next;
    priority = Priority::Event;
    elapsed  = std::chrono::milliseconds{0};
}

Action PetStateMachine::pickRandomIdle()
{
    std::vector<Action> choices;
    for (auto const candidate : kRandomIdle) {
        if (candidate != lastIdle) choices.push_back(candidate);
    }
    auto const index = std::min(static_cast<std::size_t>(random() * static_cast<double>(choices.size())),
                                choices.size() - 1);
    lastIdle         = choices[index];
    return choices[index];
}

std::chrono::milliseconds PetStateMachine::nextIdleWait()
{
    auto const jitter = static_cast<std::chrono::milliseconds::rep>(random() * static_cast<double>(kIdleCooldownBase.count()));
    return kIdleCooldownBase + std::chrono::milliseconds{jitter};
}

std::chrono::milliseconds PetStateMachine::durationOf(Action action) const
{
    auto const& spec = specs.at(action);
    return std::chrono::milliseconds{spec.frames * 1000 / spec.fps};
}

} // namespace lens::core::pet
