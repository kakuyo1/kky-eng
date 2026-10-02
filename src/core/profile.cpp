#include "core/profile.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace lens::core::profile {
namespace {

using Registry = std::unordered_map<const char*, Stat>;

/**
 * @brief Every instrument site seen so far.
 *
 * Keyed by the literal's address: the macros only ever pass string literals, so the pointer
 * identifies the site and no key has to be hashed or copied per sample.
 *
 * ponytail: one registry, no lock. lens_core is single-threaded; add a mutex only when a
 * worker thread starts calling into the pipeline.
 */
Registry& registry() {
    static Registry sites;
    return sites;
}

/// @return The instrument keys, ordered by name so two runs diff cleanly.
std::vector<const char*> sortedKeys(const Registry& sites) {
    std::vector<const char*> keys;
    keys.reserve(sites.size());
    for (const auto& [name, stat] : sites) keys.push_back(name);

    std::sort(keys.begin(), keys.end(),
              [](const char* a, const char* b) { return std::strcmp(a, b) < 0; });
    return keys;
}

/// @brief Write the header and one row for every key in @p keys.
void writeTable(std::ostringstream& out, const Registry& sites, const std::vector<const char*>& keys,
                bool timed) {
    constexpr int kNameWidth = 30;
    constexpr int kNumberWidth = 16;

    out << std::left << std::setw(kNameWidth) << (timed ? "timer" : "counter") << std::right;
    out << std::setw(kNumberWidth) << "count";
    if (timed) {
        out << std::setw(kNumberWidth) << "total ms" << std::setw(kNumberWidth) << "mean ns"
            << std::setw(kNumberWidth) << "max ns";
    }
    out << '\n';

    for (const char* key : keys) {
        const Stat& stat = sites.at(key);
        if (stat.timed != timed) continue;

        out << std::left << std::setw(kNameWidth) << key << std::right;
        out << std::setw(kNumberWidth) << stat.count;
        if (timed) {
            const auto mean = stat.count == 0 ? 0.0
                                              : static_cast<double>(stat.totalNs) /
                                                    static_cast<double>(stat.count);
            out << std::fixed << std::setprecision(3) << std::setw(kNumberWidth)
                << static_cast<double>(stat.totalNs) / 1e6 << std::setprecision(0)
                << std::setw(kNumberWidth) << mean << std::setw(kNumberWidth) << stat.maxNs;
        }
        out << '\n';
    }
}

}   // namespace

ScopeTimer::ScopeTimer(const char* name_) : name(name_), startedAt(std::chrono::steady_clock::now()) {}

ScopeTimer::~ScopeTimer() {
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - startedAt)
                             .count();
    const auto ns = elapsed < 0 ? std::uint64_t{0} : static_cast<std::uint64_t>(elapsed);

    Stat& stat = registry()[name];
    stat.timed = true;
    ++stat.count;
    stat.totalNs += ns;
    stat.maxNs = std::max(stat.maxNs, ns);
}

void count(const char* name, std::uint64_t n) { registry()[name].count += n; }

std::string report() {
    const Registry& sites = registry();
    if (sites.empty()) return {};

    const auto keys = sortedKeys(sites);

    std::ostringstream out;
    out << "lens_core profile: " << sites.size() << " site(s)\n";
    writeTable(out, sites, keys, true);
    writeTable(out, sites, keys, false);
    return out.str();
}

void reset() { registry().clear(); }

}
