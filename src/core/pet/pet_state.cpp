/**
 * @file pet_state.cpp
 * @brief Implementation of PetStateMachine: the priority rules, held states, and the random idle pick.
 */

#include "core/pet/pet_state.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace lens::core::pet {

namespace {

/// Actions the idle timer picks from (PHASE3 3.3: random idle chooses among the situational actions).
constexpr std::array<Action, 2> kSituationalIdle{Action::LookAround, Action::Stretch};

} // namespace

PetStateMachine::PetStateMachine(ActionSpecs const& specs_, RandomSource const& random_)
    : specs{specs_}, random{random_}
{
    enter(Action::Idle, Priority::Idle);
}

bool PetStateMachine::blinking() const
{
    return specs.at(current).blinks;
}

void PetStateMachine::handle(PetEvent event)
{
    pendingReturn.reset();
    quiet = std::chrono::milliseconds{0};
    switch (event) {
        case PetEvent::DragStart:
            if (dragging) return;
            dragging = true;
            enter(Action::Pickup, Priority::Drag);
            return;
        case PetEvent::DragEnd:
            if (not dragging) return;
            dragging = false;
            enter(Action::Idle, Priority::Idle);
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
            hold(Action::Study);
            return;
        case PetEvent::ExplanationHidden:
            release(Action::Study);
            return;
        case PetEvent::KnownMarked:
        case PetEvent::UpdateAvailable:
            request(Action::Celebrate, Priority::Event);
            return;
        case PetEvent::NewWordMarked:
            request(Action::Encourage, Priority::Event);
            return;
        case PetEvent::BudgetPaused:
            hold(Action::Sleep);
            return;
        case PetEvent::BudgetResumed:
            release(Action::Sleep);
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
        idleMs += step;
        if (idleMs >= idleWait) {
            if (auto const pick = pickRandomIdle()) request(*pick, Priority::Situational);
        }
        return;
    }
    elapsed += step;
    if (current == Action::Thinking) {
        if (elapsed >= kThinkingTimeout) settle();
        return;
    }
    if (not specs.at(current).loop and elapsed >= durationOf(current)) settle();
}

void PetStateMachine::request(Action next, Priority requested)
{
    // Thinking waits for the explanation that the event completes, so any event replaces it.
    auto const givesWayToThinking = current == Action::Thinking and requested == Priority::Event;
    if (requested <= priority and not givesWayToThinking) return;
    enter(next, requested);
}

void PetStateMachine::enter(Action next, Priority newPriority)
{
    current       = next;
    priority      = newPriority;
    elapsed       = std::chrono::milliseconds{0};
    pendingReturn = specs.at(next).returnTo;
    if (next == Action::Idle) {
        idleMs   = std::chrono::milliseconds{0};
        idleWait = nextIdleWait();
    }
}

void PetStateMachine::hold(Action state)
{
    held = state;
    request(state, Priority::Event);
}

void PetStateMachine::release(Action state)
{
    if (held == state) held.reset();
    if (current == state) settle();
}

void PetStateMachine::settle()
{
    // A yawn's return to sleep is situational, so a learning event can still wake the dog into reading.
    // A held state returns at event priority, as it started.
    if (pendingReturn) {
        enter(*pendingReturn, Priority::Situational);
        return;
    }
    if (held) {
        enter(*held, Priority::Event);
        return;
    }
    enter(Action::Idle, Priority::Idle);
}

std::optional<Action> PetStateMachine::pickRandomIdle()
{
    // Never the action just played, and only actions that have a playback (PHASE3 3.1, 3.3).
    std::vector<Action> choices;
    for (auto const candidate : kSituationalIdle) {
        if (specs.count(candidate) != 0 and candidate != lastIdle) choices.push_back(candidate);
    }
    if (choices.empty()) return std::nullopt;

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
