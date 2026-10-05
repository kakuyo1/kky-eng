#include "core/stats_store.h"
#include "core/log.h"

#include <algorithm>
#include <unordered_set>

namespace lens::core {
namespace {

/// Length of the date part of a stored minute, "YYYY-MM-DD".
constexpr std::size_t kDateLen = 10;

/// @return The date part of a stored minute, or an empty string when it is too short to hold one.
std::string dateOf(const std::string& minute)
{
    return minute.size() >= kDateLen ? minute.substr(0, kDateLen) : std::string();
}

/// @brief Move one word in or out of a day's known / new tally.
///
/// @param delta +1 when a verdict is attached to the day, -1 when one is taken back off it.
/// @note The clamp is for documents an earlier version wrote, which booked the count on the
///       day of the mark rather than the day of the pop: taking a verdict back can reach a
///       bucket that never held it.
void tallyVerdict(DailyUsage& day, const std::string& verdict, int delta)
{
    if (verdict == "known")
        day.learned = std::max(0, day.learned + delta);
    else if (verdict == "new")
        day.fresh = std::max(0, day.fresh + delta);
}

/// @brief Read one `daily` entry, tolerating anything malformed.
DailyUsage dailyFromJson(const nlohmann::json& j)
{
    DailyUsage day;
    if (!j.is_object())
        return day;
    day.pops             = j.value("pops", 0);
    day.learned          = j.value("learned", 0);
    day.fresh            = j.value("fresh", 0);
    day.promptTokens     = j.value("promptTokens", 0LL);
    day.completionTokens = j.value("completionTokens", 0LL);
    return day;
}

}

StatsStore::StatsStore(nlohmann::json& document)
    : doc_(document)
{
    if (doc_.contains("history") && doc_["history"].is_array()) {
        for (const nlohmann::json& entry : doc_["history"]) {
            if (!entry.is_object())
                continue;
            const std::string lemma  = entry.value("word", std::string());
            const std::string minute = entry.value("time", std::string());
            if (lemma.empty() || dateOf(minute).empty())
                continue; // a record with no word or no usable time is not one we can show
            history_.push_back(HistoryEntry{lemma, minute, entry.value("verdict", std::string())});
        }
    }

    if (doc_.contains("daily") && doc_["daily"].is_object()) {
        for (auto it = doc_["daily"].begin(); it != doc_["daily"].end(); ++it) {
            if (it.key().size() != kDateLen)
                continue;
            daily_[it.key()] = dailyFromJson(it.value());
        }
    }

    LENS_INFO("stats loaded: history={} days={}", history_.size(), daily_.size());
}

void StatsStore::recordPop(std::string lemma, std::string minute)
{
    history_.insert(history_.begin(), HistoryEntry{std::move(lemma), std::move(minute), std::string()});
    if (history_.size() > kMaxHistory) {
        LENS_DEBUG("stats history over {} entries; dropping the oldest", kMaxHistory);
        history_.resize(kMaxHistory);
    }
    daily_[dateOf(history_.front().minute)].pops++;
    writeBack();
}

void StatsStore::recordVerdict(const std::string& lemma, std::string minute, std::string verdict)
{
    // The newest entry for the lemma, not the newest *unmarked* one: the newest entry is what
    // words() reads, so it is the one a mark has to reach. Looking for an unmarked one made the
    // words popup's verdict pills write-once -- pressing one on a word that already carried a
    // verdict found nothing to fill, so the row did not move.
    const auto entry = std::find_if(history_.begin(), history_.end(), [&](const HistoryEntry& e) { return e.lemma == lemma; });

    if (entry == history_.end()) {
        // The pop may have aged out of the history window. The tally still moved, and with no
        // entry left to say which day the word was shown, it lands on the day of the mark.
        LENS_DEBUG("stats: no history entry for '{}'; only the daily tally moves", lemma);
        tallyVerdict(daily_[dateOf(minute)], verdict, 1);
        writeBack();
        return;
    }

    if (entry->verdict == verdict)
        return; // the word already carries this verdict; nothing moved, so the day does not grow

    // The count belongs to the day the word was shown -- the panel reads "of today's pops, how
    // many you settled this way" -- which is also the only day it can be taken back off. So a
    // re-marked word moves from one column to the other instead of landing in both, and the day
    // that counted it is the day that gives it up.
    DailyUsage& day = daily_[dateOf(entry->minute)];
    tallyVerdict(day, entry->verdict, -1);
    entry->verdict = verdict; // keep the pop's own minute; this is when it was shown
    tallyVerdict(day, verdict, 1);
    writeBack();
}

void StatsStore::recordUsage(const std::string& minute, long long promptTokens, long long completionTokens)
{
    DailyUsage& day = daily_[dateOf(minute)];
    day.promptTokens += promptTokens;
    day.completionTokens += completionTokens;
    writeBack();
}

std::string exportLemmas(const std::vector<HistoryEntry>& history, ExportScope scope)
{
    const auto wanted = [scope](const HistoryEntry& entry) {
        if (scope == ExportScope::Known) return entry.verdict == "known";
        if (scope == ExportScope::New) return entry.verdict == "new";
        return true;
    };

    // One line per lemma, taken at its newest entry -- the same dedup the words popup does,
    // so a word shown twice is one line here as it is one row there.
    std::unordered_set<std::string> seen;
    std::string text;
    for (const HistoryEntry& entry : history) {
        if (!seen.insert(entry.lemma).second || !wanted(entry))
            continue;
        text += entry.lemma;
        text += '\n';
    }
    return text;
}

void StatsStore::writeBack()
{
    nlohmann::json history = nlohmann::json::array();
    for (const HistoryEntry& entry : history_)
        history.push_back({{"word", entry.lemma}, {"time", entry.minute}, {"verdict", entry.verdict}});
    doc_["history"] = std::move(history);

    nlohmann::json daily = nlohmann::json::object();
    for (const auto& [date, day] : daily_) {
        daily[date] = {{"pops", day.pops},
                       {"learned", day.learned},
                       {"fresh", day.fresh},
                       {"promptTokens", day.promptTokens},
                       {"completionTokens", day.completionTokens}};
    }
    doc_["daily"] = std::move(daily);
}

}
