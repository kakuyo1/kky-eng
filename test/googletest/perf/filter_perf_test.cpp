/**
 * @file filter_perf_test.cpp
 * @brief Throughput of the FilterCore hot path, plus the profile breakdown when built with
 *        -DLENS_ENABLE_PROFILE=ON.
 *
 * The numbers are the point, so this prints rather than asserts on them: wall-clock
 * thresholds would be flaky on a shared machine. What it does assert is that the workload
 * reached the pipeline at all, because a benchmark that filters everything to nothing
 * measures the hard filter and nothing else.
 *
 * Build with the instrumentation on to see where the time inside the pipeline goes:
 *
 *     ./scripts/build/build.bat -DLENS_ENABLE_PROFILE=ON
 */

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/filter_core.h"
#include "core/profile.h"
#include "support.h"

namespace {

// TEST_F pastes the fixture name into a class definition, so it has to be unqualified.
using lens::test::CoreTest;

/// @return The corpus's excerpt texts, which are real prose rather than generated filler.
std::vector<std::string> corpusTexts()
{
    std::ifstream in(lens::test::sourceDir() / "test" / "eval_corpus.json");
    if (!in) return {};

    const auto corpus = nlohmann::json::parse(in, nullptr, false);
    if (!corpus.is_array()) return {};

    std::vector<std::string> texts;
    texts.reserve(corpus.size());
    for (const auto& item : corpus)
        texts.push_back(item.at("text").get<std::string>());
    return texts;
}

} // namespace

TEST_F(CoreTest, FilterWordsThroughput)
{
    const auto texts = corpusTexts();
    ASSERT_FALSE(texts.empty()) << "the corpus gave no excerpts to measure";

    const std::unordered_set<std::string> known; // an empty known set: worst case

    // Warm up: first touch of the tables, the corpus strings, and the allocator.
    for (const auto& text : texts)
        lens::core::filterWords(text, known, 0);

    constexpr int kRounds = 200;
    std::vector<double> roundMs;
    roundMs.reserve(kRounds);
    std::size_t candidates = 0;

    lens::core::profile::reset();

    for (int round = 0; round < kRounds; ++round) {
        const auto startedAt = std::chrono::steady_clock::now();
        for (const auto& text : texts)
            candidates += lens::core::filterWords(text, known, 0).size();
        roundMs.push_back(std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - startedAt)
                              .count());
    }

    // Sorted, so front/back are the real extremes and the middle sample is the median. The
    // median is what gets quoted: it ignores the scheduler hiccup a mean would drag in.
    std::sort(roundMs.begin(), roundMs.end());
    const double medianMs = roundMs.at(roundMs.size() / 2);

    std::cout << "\ncorpus round: " << texts.size() << " excerpt(s), median " << medianMs
              << " ms  (min " << roundMs.front() << ", max " << roundMs.back() << ")\n"
              << "per excerpt: " << medianMs / static_cast<double>(texts.size()) << " ms\n"
              << "candidates kept over " << kRounds << " rounds: " << candidates << "\n";

    const std::string profile = lens::core::profile::report();
    if (profile.empty())
        std::cout << "profile: compiled out (configure with -DLENS_ENABLE_PROFILE=ON)\n";
    else
        std::cout << profile;

    EXPECT_GT(candidates, 0u) << "the workload never produced a candidate";
}
