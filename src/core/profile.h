#pragma once

#include <chrono>
#include <cstdint>
#include <string>

/**
 * @file profile.h
 * @brief Measurement points inside lens_core: per-scope timing and event counters.
 *
 * Instrumentation is compiled out unless the build defines LENS_PROFILE=1, which the CMake
 * option LENS_ENABLE_PROFILE turns on. With it off, LENS_PROFILE_SCOPE and
 * LENS_PROFILE_COUNT expand to nothing and the measured code is unchanged.
 *
 * Counters exist because the alternative -- logging from a hot loop -- floods the log and
 * perturbs the thing being measured. Sites accumulate silently, and one report() at the end
 * of a run says where the time went. Nothing here logs on its own.
 *
 * @note Single-threaded by design: the sites are written from the thread doing the work,
 *       and lens_core is single-threaded today.
 */

/// @brief Whether the instrumentation compiles in. Set by CMake; 1 enables, 0 or absent
///        disables.
#ifndef LENS_PROFILE
#define LENS_PROFILE 0
#endif

#if LENS_PROFILE

#define LENS_PROFILE_CONCAT_(a, b) a##b
#define LENS_PROFILE_CONCAT(a, b) LENS_PROFILE_CONCAT_(a, b)

/**
 * @brief Time the enclosing scope and fold the sample into @p name's statistics.
 * @param name Instrument key, a string literal. The key is kept as a pointer rather than
 *             copied, so naming a temporary would leave a dangling key.
 */
#define LENS_PROFILE_SCOPE(name) \
    ::lens::core::profile::ScopeTimer const LENS_PROFILE_CONCAT(lensProfileScope_, __LINE__)(name)

/**
 * @brief Add @p n to @p name's counter.
 *
 * For work that is not a scope: a branch taken inside a loop, or the items a scope went on
 * to process.
 *
 * @param name Instrument key, a string literal.
 * @param n    Amount to add.
 */
#define LENS_PROFILE_COUNT(name, n) ::lens::core::profile::count((name), (n))

#else

#define LENS_PROFILE_SCOPE(name) ((void)0)
#define LENS_PROFILE_COUNT(name, n) ((void)0)

#endif

namespace lens::core::profile {

/// @brief What one instrument key has accumulated.
struct Stat {
    std::uint64_t count = 0;   ///< Scope entries, or the sum of a counter's increments.
    std::uint64_t totalNs = 0; ///< Time inside the scope. Always 0 for a counter.
    std::uint64_t maxNs = 0;   ///< Longest single entry. Always 0 for a counter.
    bool timed = false;        ///< True once a ScopeTimer has written here, which is what
                               ///< keeps the report's timing columns off a pure counter.
};

/**
 * @brief RAII sample: records one entry into @p name's statistics when the scope ends.
 *
 * The two steady_clock reads are the whole cost, tens of nanoseconds. That is why a scope
 * entered once per token is worth pairing with a counter instead of being timed directly:
 * below a few hundred nanoseconds of payload the timer measures itself.
 */
struct ScopeTimer {
    /// @param name Instrument key. Not copied, so it has to outlive the timer; pass a
    ///             literal. LENS_PROFILE_SCOPE spells that at the call site.
    explicit ScopeTimer(const char* name);

    ~ScopeTimer();

    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;

private:
    const char* name;
    std::chrono::steady_clock::time_point startedAt;
};

/**
 * @brief Add @p n to @p name's counter.
 * @param name Instrument key; see ScopeTimer.
 * @param n    Amount to add.
 */
void count(const char* name, std::uint64_t n = 1);

/**
 * @brief Every site seen so far as an aligned table, sites ordered by name.
 *
 * Timers and counters are listed separately, since a counter has no timing to show. An
 * empty string means nothing has been recorded.
 */
std::string report();

/// @brief Drop every sample, so a later report() covers only what runs after this call.
void reset();

}
