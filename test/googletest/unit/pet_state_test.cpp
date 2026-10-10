/**
 * @file pet_state_test.cpp
 * @brief PetStateMachine, offline: the clock is stepped by the test and the random source is injected.
 *
 * Covers PHASE3 3.3 and the state-machine rows of 3.9. Expected timings come from the action table
 * in the prototype (assets/pet/manifest.json), so a change there shows up here first.
 */

#include <chrono>

#include <gtest/gtest.h>

#include "core/pet/pet_state.h"

namespace {

using lens::core::pet::Action;
using lens::core::pet::ActionSpec;
using lens::core::pet::ActionSpecs;
using lens::core::pet::PetEvent;
using lens::core::pet::PetStateMachine;
using lens::core::pet::RandomSource;
using namespace std::chrono_literals;

ActionSpecs specs()
{
    return {
        {Action::Idle, ActionSpec{5, 5, true}},
        {Action::Study, ActionSpec{6, 4, true}},
        {Action::Thinking, ActionSpec{4, 3, true}},
        {Action::Celebrate, ActionSpec{8, 10, false}},
        {Action::Encourage, ActionSpec{5, 8, false}},
        {Action::Sleep, ActionSpec{4, 3, true}},
        {Action::ClickReact, ActionSpec{4, 12, false}},
        {Action::Pickup, ActionSpec{4, 6, true}},
        {Action::LookAround, ActionSpec{6, 6, false}},
        {Action::Yawn, ActionSpec{5, 6, false, Action::Sleep}},
        {Action::Stretch, ActionSpec{6, 6, false}},
    };
}

PetStateMachine makeMachine(double random = 0.0)
{
    return PetStateMachine{specs(), RandomSource{[random] { return random; }}};
}

/// Steps the clock in 100 ms ticks until `wanted` plays. Returns the time spent, capped at 300 s.
std::chrono::milliseconds runUntil(PetStateMachine& machine, Action wanted)
{
    auto elapsed = 0ms;
    while (machine.action() != wanted and elapsed < 300s) {
        machine.advance(100ms);
        elapsed += 100ms;
    }
    return elapsed;
}

TEST(PetStateMachine, StartsIdle)
{
    EXPECT_EQ(makeMachine().action(), Action::Idle);
}

TEST(PetStateMachine, ClickInterruptsReadingAndReturnsToIt)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::ExplanationShown);
    ASSERT_EQ(machine.action(), Action::Study);

    machine.handle(PetEvent::Click);
    EXPECT_EQ(machine.action(), Action::ClickReact);

    machine.advance(333ms); // 4 frames at 12 fps
    EXPECT_EQ(machine.action(), Action::Study);
}

TEST(PetStateMachine, EqualPriorityEventWaitsUntilTheOneShotEnds)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::KnownMarked);
    ASSERT_EQ(machine.action(), Action::Celebrate);

    // Reading starts at the same priority as the celebration, so it is held back until the celebration ends.
    machine.handle(PetEvent::ExplanationShown);
    EXPECT_EQ(machine.action(), Action::Celebrate);

    machine.advance(800ms);
    EXPECT_EQ(machine.action(), Action::Study);
}

TEST(PetStateMachine, SelectionDoesNotInterruptAClick)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::Click);
    machine.handle(PetEvent::SelectionShown);
    EXPECT_EQ(machine.action(), Action::ClickReact);
}

TEST(PetStateMachine, OneShotReturnsToIdleWhenNothingIsHeld)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::KnownMarked);
    machine.advance(800ms); // 8 frames at 10 fps
    EXPECT_EQ(machine.action(), Action::Idle);
}

TEST(PetStateMachine, ThinkingGivesWayToTheExplanation)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::ExplanationRequested);
    ASSERT_EQ(machine.action(), Action::Thinking);

    machine.handle(PetEvent::ExplanationShown);
    EXPECT_EQ(machine.action(), Action::Study);
}

TEST(PetStateMachine, ExplanationHiddenEndsReadingAndReturnsToIdle)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::ExplanationShown);
    machine.handle(PetEvent::ExplanationHidden);
    EXPECT_EQ(machine.action(), Action::Idle);
}

TEST(PetStateMachine, DragInterruptsEverythingAndReleaseGoesToIdle)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::ExplanationShown);

    machine.handle(PetEvent::DragStart);
    ASSERT_EQ(machine.action(), Action::Pickup);

    machine.handle(PetEvent::Click);
    machine.advance(10s);
    EXPECT_EQ(machine.action(), Action::Pickup);

    // Release returns to idle, not to the reading that was playing before the grab.
    machine.handle(PetEvent::DragEnd);
    EXPECT_EQ(machine.action(), Action::Idle);
}

TEST(PetStateMachine, BudgetPauseSleepsUntilResumed)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::BudgetPaused);
    ASSERT_EQ(machine.action(), Action::Sleep);

    machine.advance(10s);
    EXPECT_EQ(machine.action(), Action::Sleep);

    machine.handle(PetEvent::BudgetResumed);
    EXPECT_EQ(machine.action(), Action::Idle);
}

TEST(PetStateMachine, ClickWakesSleepAndTheSleepReturns)
{
    auto machine = makeMachine();
    machine.handle(PetEvent::BudgetPaused);
    machine.handle(PetEvent::Click);
    ASSERT_EQ(machine.action(), Action::ClickReact);

    machine.advance(333ms);
    EXPECT_EQ(machine.action(), Action::Sleep);
}

TEST(PetStateMachine, IdleCooldownIsTwentyToFortySeconds)
{
    auto shortest = makeMachine(0.0);
    shortest.advance(19'900ms);
    EXPECT_EQ(shortest.action(), Action::Idle);
    shortest.advance(100ms);
    EXPECT_EQ(shortest.action(), Action::LookAround);

    // The same source picks the action, so 0.999 also lands on the last choice: stretch.
    auto longest = makeMachine(0.999);
    longest.advance(39'900ms);
    EXPECT_EQ(longest.action(), Action::Idle);
    longest.advance(100ms);
    EXPECT_EQ(longest.action(), Action::Stretch);
}

TEST(PetStateMachine, RandomIdleDoesNotRepeatTheLastPick)
{
    // The random source always returns 0, so the first pick is the first choice: look around.
    auto machine = makeMachine(0.0);
    machine.advance(20s);
    ASSERT_EQ(machine.action(), Action::LookAround);

    machine.advance(1s); // 6 frames at 6 fps
    ASSERT_EQ(machine.action(), Action::Idle);

    // The next pick must differ from the last one, so it is the other choice: stretch.
    machine.advance(20s);
    EXPECT_EQ(machine.action(), Action::Stretch);
}

TEST(PetStateMachine, ThreeMinutesOfQuietYawnsThenSleeps)
{
    auto machine       = makeMachine(0.999);
    auto const elapsed = runUntil(machine, Action::Yawn);
    EXPECT_GE(elapsed, 180s);
    EXPECT_LE(elapsed, 181s);

    machine.advance(833ms); // 5 frames at 6 fps
    EXPECT_EQ(machine.action(), Action::Sleep);
}

TEST(PetStateMachine, ClickDuringYawnCancelsTheReturnToSleep)
{
    auto machine = makeMachine(0.999);
    runUntil(machine, Action::Yawn);

    machine.handle(PetEvent::Click);
    ASSERT_EQ(machine.action(), Action::ClickReact);

    machine.advance(333ms);
    EXPECT_EQ(machine.action(), Action::Idle);
}

} // namespace
