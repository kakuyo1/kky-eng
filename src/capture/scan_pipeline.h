/** @file scan_pipeline.h
 * @brief Local-only scanning gates, candidate filtering and bounded batch admission.
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "core/filter_core.h"

namespace lens::capture {

using ScanTime = std::chrono::steady_clock::time_point;

struct FilterPolicy {
    int minimumLength = 3;
    std::unordered_set<std::string> knownLemmas;
    std::size_t minimumRank = 0;
};

struct ScanFrame {
    std::string context;
    std::string processName;
    bool ownProcess      = false;
    std::uint64_t pixels = 0;
};

std::string normalizedText(std::string_view text);
std::uint64_t textFingerprint(std::string_view text);
bool allowedProcess(std::string_view process, std::vector<std::string> const& whitelist, bool own);
std::vector<core::Candidate> filterProse(std::string_view text, FilterPolicy const& policy);
/// @return Count of locally recognized entity-shaped phrases which the scan channel cannot send.
std::size_t unsupportedEntityCount(std::string_view text);

struct ScanPolicy {
    int stableFrames              = 3;
    std::size_t maximumCandidates = 5;
    std::chrono::milliseconds requestInterval{2000};
    std::chrono::milliseconds cooldown{30000};
};

struct ScanPipeline {
    virtual ~ScanPipeline()                                                                  = default;
    virtual bool allows(ScanFrame const& frame) const                                        = 0;
    virtual void setWhitelist(std::vector<std::string> whitelist)                            = 0;
    virtual bool needsOcr(ScanFrame const& frame, ScanTime now)                              = 0;
    virtual void acceptText(std::string_view text, FilterPolicy const& policy, ScanTime now) = 0;
    virtual std::vector<core::Candidate> takeBatch(ScanTime now)                             = 0;
    virtual void reset()                                                                     = 0;
};

std::unique_ptr<ScanPipeline> makeScanPipeline(ScanPolicy const& policy = {});

}
