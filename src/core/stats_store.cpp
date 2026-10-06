#include "core/stats_store.h"
#include "core/log.h"
#include "util/text.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace lens::core {
namespace {

/// @brief Move one word in or out of a day's known / new tally.
///
/// @param delta +1 when a verdict is attached to the day, -1 when one is taken back off it.
/// @note The clamp is for documents an earlier version wrote, which booked the count on the
///       day of the mark rather than the day of the pop: taking a verdict back can reach a
///       bucket that never held it.
void tallyVerdict(DailyUsage& day, const std::string& verdict, int delta)
{
    if (verdict == "known")
        day.learned = util::clampedAdd(day.learned, delta);
    else if (verdict == "new")
        day.fresh = util::clampedAdd(day.fresh, delta);
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
    if (j.contains("models") && j["models"].is_object()) {
        for (auto it = j["models"].begin(); it != j["models"].end(); ++it) {
            if (!it.value().is_object())
                continue;
            day.models[it.key()] = ModelUsage{it.value().value("promptTokens", 0LL),
                                              it.value().value("completionTokens", 0LL)};
        }
    }
    if (day.models.empty() && (day.promptTokens != 0 || day.completionTokens != 0))
        day.models[kLegacyUsageModel] = ModelUsage{day.promptTokens, day.completionTokens};
    return day;
}

}

StatsStore::StatsStore(nlohmann::json& document)
    : doc_(document)
{
    if (doc_.contains("dailyBudget") && doc_["dailyBudget"].is_number()) {
        const double amount = doc_["dailyBudget"].get<double>();
        if (std::isfinite(amount) && amount >= 0.0)
            dailyBudget_ = amount;
    }

    if (doc_.contains("history") && doc_["history"].is_array()) {
        for (const nlohmann::json& entry : doc_["history"]) {
            if (!entry.is_object())
                continue;
            const std::string lemma  = entry.value("word", std::string());
            const std::string minute = entry.value("time", std::string());
            if (lemma.empty() || util::datePart(minute).empty())
                continue; // a record with no word or no usable time is not one we can show
            history_.push_back(HistoryEntry{lemma, minute, entry.value("verdict", std::string())});
        }
    }

    if (doc_.contains("daily") && doc_["daily"].is_object()) {
        for (auto it = doc_["daily"].begin(); it != doc_["daily"].end(); ++it) {
            if (it.key().size() != 10)
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
    daily_[util::datePart(history_.front().minute)].pops++;
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
        tallyVerdict(daily_[util::datePart(minute)], verdict, 1);
        writeBack();
        return;
    }

    if (entry->verdict == verdict)
        return; // the word already carries this verdict; nothing moved, so the day does not grow

    // The count belongs to the day the word was shown -- the panel reads "of today's pops, how
    // many you settled this way" -- which is also the only day it can be taken back off. So a
    // re-marked word moves from one column to the other instead of landing in both, and the day
    // that counted it is the day that gives it up.
    DailyUsage& day = daily_[util::datePart(entry->minute)];
    tallyVerdict(day, entry->verdict, -1);
    entry->verdict = verdict; // keep the pop's own minute; this is when it was shown
    tallyVerdict(day, verdict, 1);
    writeBack();
}

void StatsStore::recordUsage(const std::string& minute, long long promptTokens, long long completionTokens)
{
    recordUsage(minute, promptTokens, completionTokens, kLegacyUsageModel);
}

void StatsStore::recordUsage(const std::string& minute,
                             long long promptTokens,
                             long long completionTokens,
                             std::string model)
{
    if (model.empty())
        model = kLegacyUsageModel;
    DailyUsage& day = daily_[util::datePart(minute)];
    day.promptTokens += promptTokens;
    day.completionTokens += completionTokens;
    auto& bucket = day.models[std::move(model)];
    bucket.promptTokens += promptTokens;
    bucket.completionTokens += completionTokens;
    writeBack();
}

bool StatsStore::setDailyBudget(double amount)
{
    if (!std::isfinite(amount) || amount < 0.0)
        return false;
    dailyBudget_ = amount;
    writeBack();
    return true;
}

bool StatsStore::removeLemma(const std::string& lemma)
{
    bool removed = false;
    for (const auto& entry : history_) {
        if (entry.lemma != lemma)
            continue;
        auto const day = daily_.find(util::datePart(entry.minute));
        if (day != daily_.end()) {
            day->second.pops = util::clampedAdd(day->second.pops, -1);
            tallyVerdict(day->second, entry.verdict, -1);
        }
        removed = true;
    }
    if (!removed)
        return false;

    history_.erase(std::remove_if(history_.begin(), history_.end(), [&](const HistoryEntry& entry) {
                       return entry.lemma == lemma;
                   }),
                   history_.end());
    writeBack();
    return true;
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
        daily[date]           = {{"pops", day.pops},
                                 {"learned", day.learned},
                                 {"fresh", day.fresh},
                                 {"promptTokens", day.promptTokens},
                                 {"completionTokens", day.completionTokens}};
        daily[date]["models"] = nlohmann::json::object();
        for (const auto& [model, usage] : day.models)
            daily[date]["models"][model] = {{"promptTokens", usage.promptTokens},
                                            {"completionTokens", usage.completionTokens}};
    }
    doc_["daily"]       = std::move(daily);
    doc_["dailyBudget"] = dailyBudget_;
}

}
