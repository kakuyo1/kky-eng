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
 * advances the frame clock and reads action() and blinking(). It sees event types only, never text
 * or selections.
 */

namespace lens::core::pet {

/// @brief Every action the state machine can request. Blinking runs on the expression layer and is not one of them.
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

/// @brief What happened. A bare enum, so no word, selection or explanation text can travel with it (PHASE3 3.9).
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

/// @brief Playback of one action, from animations.json.
struct ActionSpec {
    int frames;
    int fps;
    bool loop;
    /// Where a one-shot goes when it ends. Without it the dog returns to the held state, or to idle.
    std::optional<Action> returnTo;
    /// Whether the expression layer blinks while this action plays (PHASE3 3.3).
    bool blinks;
};

/// @brief Playback for every action the machine can request. A missing entry is a configuration error.
using ActionSpecs = std::map<Action, ActionSpec>;

/// @brief Returns a number in [0, 1). Injected so the idle pick and its cooldown are testable.
using RandomSource = std::function<double()>;

/**
 * @brief Chooses the action the dog plays, from events and elapsed time.
 *
 * Priority, highest first: drag, click, event, situational, idle (PHASE3 3.3). A request at the same
 * priority as the action now playing, or below it, is dropped. Thinking is the exception: it only waits
 * for the explanation that an event completes, so any event replaces it. Thinking also ends by itself
 * after kThinkingTimeout, because the specification has no event for a request that is never answered.
 *
 * Reading and sleeping are held states: an event keeps them, and the dog returns to them after a
 * one-shot reaction ends. Dragging is not held, so releasing the dog always lands on idle.
 */
struct PetStateMachine {
    /// @brief Quiet time with no event before the dog yawns and falls asleep.
    static constexpr std::chrono::milliseconds kQuietBeforeYawn{180'000};
    /// @brief Idle wait before a random action: this base, plus up to the same again at random.
    static constexpr std::chrono::milliseconds kIdleCooldownBase{20'000};
    /// @brief How long thinking waits for its explanation before it gives up.
    static constexpr std::chrono::milliseconds kThinkingTimeout{30'000};

    /// @param specs Playback of every action; must hold an entry for each Action the machine plays.
    /// @param random Random source in [0, 1).
    PetStateMachine(ActionSpecs const& specs, RandomSource const& random);

    /// @return The action being played now.
    Action action() const
    {
        return current;
    }

    /// @return Whether the expression layer should blink now, from the current action's `blinks` flag.
    bool blinking() const;

    /// @brief Applies one event. A drag ends only through DragEnd.
    /// @param event What happened. Carries no text.
    void handle(PetEvent event);

    /// @brief Moves the clock on by `step`: starts quiet or idle actions when due, and ends actions that have run out.
    /// @param step Time since the last call.
    void advance(std::chrono::milliseconds step);

private:
    enum class Priority : std::uint8_t {
        Idle,
        Situational,
        Event,
        Click,
        Drag,
    };

    /// @brief Starts `next` if its priority is above the action now playing, or if thinking gives way to it.
    void request(Action next, Priority requested);
    /// @brief Starts `next` at `priority`, resetting the elapsed time and the pending return from its `returnTo`.
    void enter(Action next, Priority priority);
    /// @brief Holds `state` (reading or sleeping) and starts it at event priority.
    void hold(Action state);
    /// @brief Releases `state`, and ends it if it is playing.
    void release(Action state);
    /// @brief Ends a one-shot or a held state: to the pending return, else the held state, else idle.
    void settle();
    /// @return The next idle action, or nothing when every candidate is the one just played or missing.
    std::optional<Action> pickRandomIdle();
    std::chrono::milliseconds nextIdleWait();
    std::chrono::milliseconds durationOf(Action action) const;

    ActionSpecs specs;
    RandomSource random;
    Action current{Action::Idle};
    Priority priority{Priority::Idle};
    /// Time spent in the current action.
    std::chrono::milliseconds elapsed{0};
    /// Time since the last event, for the quiet yawn.
    std::chrono::milliseconds quiet{0};
    /// Time spent idle, for the random pick.
    std::chrono::milliseconds idleMs{0};
    std::chrono::milliseconds idleWait{0};
    /// The held state an event keeps: reading while an explanation is up, sleep while the budget is paused.
    std::optional<Action> held;
    /// Where the current action goes when it ends, from its `returnTo`. Any event clears it first, so a
    /// reaction cancels a yawn's return to sleep.
    std::optional<Action> pendingReturn;
    std::optional<Action> lastIdle;
    bool dragging{false};
};

} // namespace lens::core::pet
