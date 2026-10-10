#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>

/**
 * @file pet_state.h
 * @brief The desktop pet's action state machine (PHASE3 3.2 and 3.3).
 *
 * Pure logic: no Qt, no timers, no window. The machine chooses the action; the Qt controller
 * advances the frame clock and reads action(). It sees event types only, never text or selections.
 */

namespace lens::core::pet {

/// @brief Every action the dog plays. Blinking runs on the expression layer and is not listed here.
enum class Action : std::uint8_t {
    Idle,
    Study,
    Thinking,
    Celebrate,
    Encourage,
    Sleep,
    ClickReact,
    Pickup,
    LookAround,
    Yawn,
    Stretch,
};

/// @brief What happened. Carries no word, selection or explanation text (PHASE3 3.2, 3.9).
enum class PetEvent : std::uint8_t {
    DragStart,
    DragEnd,
    Click,
    SelectionShown,
    ExplanationRequested,
    ExplanationShown,
    ExplanationHidden,
    KnownMarked,
    NewWordMarked,
    BudgetPaused,
    BudgetResumed,
    UpdateAvailable,
};

/// @brief Playback of one action, from animations.json: its frame count, its frame rate, whether it repeats,
///        and where a one-shot goes when it ends. Without `returnTo` it goes back to the held state, or idle.
struct ActionSpec {
    int frames;
    int fps;
    bool loop;
    std::optional<Action> returnTo;
};

/// @brief Playback for every action the machine can request. A missing entry is a configuration error.
using ActionSpecs = std::map<Action, ActionSpec>;

/// @brief Returns a number in [0, 1). Injected so the idle pick and its cooldown are testable.
using RandomSource = std::function<double()>;

/**
 * @brief Chooses the action the dog plays, from events and elapsed time.
 *
 * Priority, highest first: drag, click, event, situational, idle (PHASE3 3.3). A request at the same
 * priority as the action now playing, or below it, is dropped. The one exception is thinking, which
 * gives way to any event, since it only waits for the explanation that the event completes.
 *
 * Reading and sleeping are held states: an event keeps them, and the dog returns to them after a
 * one-shot reaction ends. Dragging is not held, so releasing the dog always lands on idle.
 */
struct PetStateMachine {
    /// @brief Quiet time with no event before the dog yawns and falls asleep.
    static constexpr std::chrono::milliseconds kQuietBeforeYawn{180'000};
    /// @brief Idle wait before a random action: this base, plus up to the same again at random.
    static constexpr std::chrono::milliseconds kIdleCooldownBase{20'000};

    /// @param specs Playback of every action; must hold an entry for each Action.
    /// @param random Random source in [0, 1).
    PetStateMachine(ActionSpecs specs, RandomSource random);

    /// @return The action being played now.
    Action action() const
    {
        return current;
    }

    /// @brief Applies one event. A drag ends only through DragEnd.
    void handle(PetEvent event);

    /// @brief Moves the clock on by `step`. Ends one-shot actions, and starts quiet or idle actions when due.
    void advance(std::chrono::milliseconds step);

private:
    enum class Priority : std::uint8_t {
        Idle,
        Situational,
        Event,
        Click,
        Drag,
    };

    /// @return Whether the request was accepted and `next` is now the action being played.
    bool request(Action next, Priority requested);
    void enterIdle();
    /// @brief Ends a one-shot or a held state: back to the state an event asked for, else idle.
    void settle();
    Action pickRandomIdle();
    std::chrono::milliseconds nextIdleWait();
    std::chrono::milliseconds durationOf(Action action) const;

    ActionSpecs specs;
    RandomSource random;
    Action current{Action::Idle};
    Priority priority{Priority::Idle};
    std::chrono::milliseconds elapsed{0};
    std::chrono::milliseconds quiet{0};
    std::chrono::milliseconds idle{0};
    std::chrono::milliseconds idleWait{0};
    /// The held state an event keeps: reading while an explanation is up, sleep while the budget is paused.
    std::optional<Action> held;
    /// Where the current action goes when it ends, from its `returnTo`. Any event clears it first, so a
    /// reaction cancels a yawn's return to sleep.
    std::optional<Action> resume;
    std::optional<Action> lastIdle;
    bool dragging{false};
};

} // namespace lens::core::pet
