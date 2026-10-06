/** @file scan_pipeline.cpp
 * @brief Foreground, pixel, text-stability and candidate admission gates.
 */
#include "scan_pipeline.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <iterator>
#include <stdexcept>
#include <utility>

#include "capture_policy.h"

namespace lens::capture {
namespace {

bool letter(char const c)
{
    return (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z');
}

bool isTitleCase(std::string_view token)
{
    if (token.size() < 2 or not letter(token.front()) or token.front() < 'A' or token.front() > 'Z') return false;
    for (std::size_t i = 1; i < token.size(); ++i)
        if (token[i] < 'a' or token[i] > 'z') return false;
    return true;
}

bool isAcronym(std::string_view token)
{
    if (token.size() < 2) return false;
    return std::all_of(token.begin(), token.end(), [](char const c) { return c >= 'A' and c <= 'Z'; });
}

std::string basename(std::string_view const path)
{
    auto const at = path.find_last_of("/\\");
    auto name     = std::string{at == std::string_view::npos ? path : path.substr(at + 1)};
    for (auto& c : name)
        if (c >= 'A' and c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return name;
}

struct ScanPipelineImpl final : ScanPipeline {
    explicit ScanPipelineImpl(ScanPolicy const& policy_)
        : policy{policy_}
    {
        if (policy.stableFrames < 2 or policy.stableFrames > 10 or policy.maximumCandidates == 0 or
            policy.maximumCandidates > 32 or policy.requestInterval < std::chrono::seconds{2} or
            policy.cooldown < policy.requestInterval)
            throw std::invalid_argument{"Invalid scan policy"};
    }

    bool allows(ScanFrame const& frame) const override
    {
        return not frame.context.empty() and allowedProcess(frame.processName, whitelist, frame.ownProcess);
    }

    void setWhitelist(std::vector<std::string> whitelist_) override
    {
        whitelist = std::move(whitelist_);
        reset();
    }

    bool needsOcr(ScanFrame const& frame, ScanTime const now) override
    {
        if (not allows(frame)) {
            reset();
            return false;
        }
        if (context != frame.context) {
            reset();
            context = frame.context;
        }
        if (lastSample and now - *lastSample < std::chrono::seconds{1}) return false;
        lastSample         = now;
        bool const changed = not pixels or * pixels != frame.pixels;
        pixels             = frame.pixels;
        if (changed) pending.clear();
        awaitingText = changed or matches < policy.stableFrames;
        return awaitingText;
    }

    void acceptText(std::string_view const text, FilterPolicy const& filter, ScanTime const now) override
    {
        if (context.empty() or not awaitingText) return;
        awaitingText = false;
        if (text.size() > 65536) {
            reset();
            return;
        }
        auto current = normalizedText(text);
        if (current.empty()) {
            previous.clear();
            matches = 0;
            pending.clear();
            return;
        }
        auto const fingerprint = textFingerprint(current);
        if (fingerprint != previousFingerprint or current != previous) {
            previousFingerprint = fingerprint;
            previous            = std::move(current);
            matches             = 1;
            pending.clear();
            return;
        }
        matches = std::min(matches + 1, policy.stableFrames);
        if (matches < policy.stableFrames) return;
        std::erase_if(recent, [&](auto const& entry) { return now - entry.at >= policy.cooldown; });
        if (std::any_of(recent.begin(), recent.end(), [&](auto const& entry) { return entry.text == previous; })) return;

        pending = filterProse(text, filter);
        std::erase_if(pending, [](core::Candidate const& word) { return word.state != core::CandidateState::New; });
        if (pending.size() > policy.maximumCandidates) pending.resize(policy.maximumCandidates);
        if (recent.size() == 16) recent.erase(recent.begin());
        recent.push_back({.text = previous, .at = now});
    }

    std::vector<core::Candidate> takeBatch(ScanTime const now) override
    {
        if (pending.empty() or (lastRequest and now - *lastRequest < policy.requestInterval)) return {};
        lastRequest = now;
        return std::exchange(pending, {});
    }

    void reset() override
    {
        context.clear();
        pixels.reset();
        lastSample.reset();
        previous.clear();
        recent.clear();
        matches      = 0;
        awaitingText = false;
        pending.clear();
    }

    ScanPolicy policy;
    std::vector<std::string> whitelist;
    std::string context;
    std::optional<std::uint64_t> pixels;
    std::optional<ScanTime> lastSample;
    std::optional<ScanTime> lastRequest;
    std::string previous;
    struct CooldownEntry {
        std::string text;
        ScanTime at;
    };
    std::vector<CooldownEntry> recent;
    int matches                       = 0;
    std::uint64_t previousFingerprint = 0;
    bool awaitingText                 = false;
    std::vector<core::Candidate> pending;
};

}

std::string normalizedText(std::string_view const text)
{
    std::string out;
    bool space = false;
    for (char const c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            space = not out.empty();
            continue;
        }
        if (space) out.push_back(' ');
        space = false;
        out.push_back(c >= 'A' and c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    }
    return out;
}

std::uint64_t textFingerprint(std::string_view const text)
{
    auto hash = std::uint64_t{14695981039346656037ULL};
    for (unsigned char const c : normalizedText(text)) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool allowedProcess(std::string_view const process, std::vector<std::string> const& whitelist, bool const own)
{
    if (own or process.empty()) return false;
    auto const name = basename(process);
    return std::any_of(whitelist.begin(), whitelist.end(), [&](auto const& item) {
        return not item.empty() and basename(item) == name;
    });
}

std::vector<core::Candidate> filterProse(std::string_view const text, FilterPolicy const& policy)
{
    auto out = core::filterWords(text, policy.knownLemmas, policy.minimumRank, minimumWordLength(policy.minimumLength), false);
    std::erase_if(out, [](core::Candidate const& word) { return word.state != core::CandidateState::New; });
    return out;
}

std::size_t unsupportedEntityCount(std::string_view const text)
{
    std::size_t count = 0;
    std::size_t start = 0;
    while (start < text.size()) {
        while (start < text.size() and std::isspace(static_cast<unsigned char>(text[start])))
            ++start;
        auto end = text.find_first_of(" \t\r\n\v\f", start);
        if (end == std::string_view::npos) end = text.size();
        if (end == start) {
            start = end + 1;
            continue;
        }
        auto const token = text.substr(start, end - start);
        auto const first = token.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        if (first == std::string_view::npos) {
            start = end;
            continue;
        }
        auto const last  = token.find_last_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        auto const clean = token.substr(first, last - first + 1);
        if (isTitleCase(clean) or isAcronym(clean)) {
            auto next = end;
            while (next < text.size() and std::isspace(static_cast<unsigned char>(text[next])))
                ++next;
            auto nextEnd = text.find_first_of(" \t\r\n\v\f", next);
            if (nextEnd == std::string_view::npos) nextEnd = text.size();
            if (next < nextEnd) {
                auto const nextToken = text.substr(next, nextEnd - next);
                auto const nextFirst = nextToken.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
                if (nextFirst == std::string_view::npos) {
                    start = nextEnd;
                    continue;
                }
                auto const nextLast  = nextToken.find_last_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
                auto const nextClean = nextToken.substr(nextFirst, nextLast - nextFirst + 1);
                if (isTitleCase(nextClean) or isAcronym(nextClean)) {
                    ++count;
                    start = nextEnd;
                    continue;
                }
            }
        }
        start = end;
    }
    return count;
}

std::unique_ptr<ScanPipeline> makeScanPipeline(ScanPolicy const& policy)
{
    return std::make_unique<ScanPipelineImpl>(policy);
}

}
