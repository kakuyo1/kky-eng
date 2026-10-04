#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

/**
 * @file stats_store.h
 * @brief What the statistics, words, and cost popups are built from: every word a bubble
 *        showed and a per-day tally of pops and tokens.
 *
 * This is a section of the settings document, not a file of its own. It binds to the
 * document KnownStore owns and never reads or writes the file -- KnownStore::save() persists
 * the whole document, this class's keys included. Two owners of one file would each save a
 * stale view and the later save would drop the earlier one's changes.
 *
 * Only tokens are stored, never money: prices change, and a cost frozen into history means
 * nothing later. The amount is computed at display time from the current price list
 * (llm_pricing.h).
 */

namespace lens::core {

/// @brief A word the bubble showed, kept for the words popup.
struct HistoryEntry {
    std::string lemma;
    std::string minute;  ///< Local "YYYY-MM-DD HH:MM".
    std::string verdict; ///< Empty until marked, then "known" or "new".
};

/// @brief One day's tallies, for the statistics and cost popups.
struct DailyUsage {
    int pops = 0;
    int learned = 0;
    int fresh = 0;
    long long promptTokens = 0;
    long long completionTokens = 0;
};

/// @brief Which rows a words export covers.
/// @note The three values are the words popup's own filter (UI.md section 4.7), so what a
///       reader exports is what they were looking at.
enum class ExportScope : std::uint8_t { All,
                                        Known,
                                        New };

/**
 * @brief Render the history as the words export: plain text, one lemma per line.
 *
 * The rows are the words popup's: each lemma once, its newest entry deciding both the order
 * and whether it counts as known or new. Nothing here picks a file or writes one, which is
 * what keeps the text itself assertable without Qt.
 *
 * @param history The history, newest first.
 * @param scope   Every lemma, or only those the newest entry marks known / new.
 * @return The lemmas joined by '\n', ending with one; empty when no row matches @p scope.
 */
std::string exportLemmas(const std::vector<HistoryEntry>& history, ExportScope scope);

/**
 * @brief The history and daily tallies behind the statistics surfaces.
 *
 * @note Deliberately Qt-free, like KnownStore: the aggregates the popups show (this month,
 *       this week, yesterday, the daily average) are date arithmetic and live in the
 *       controller, which has QDate. Here the dates are strings.
 */
class StatsStore {
public:
    /// @brief Number of history entries kept; older ones are dropped.
    static constexpr std::size_t kMaxHistory = 2000;

    /**
     * @brief Bind to the settings document and read the keys already in it.
     * @param document The document KnownStore owns, which must outlive this object.
     */
    explicit StatsStore(nlohmann::json& document);

    /// @brief Record that a bubble showed this word.
    /// @param minute Local time as "YYYY-MM-DD HH:MM"; its date picks the daily bucket.
    void recordPop(std::string lemma, std::string minute);

    /**
     * @brief Attach the reader's verdict to the newest entry for a word.
     *
     * The newest entry is the one the words list reads, so it is the one a mark has to reach:
     * a word that already carries a verdict takes the new one, which is what makes the words
     * popup's two pills a choice rather than a write-once button.
     *
     * The mark can come long after the pop, with other words shown in between, so the entry
     * is found rather than assumed to be the last one. Finding none is not an error: the
     * verdict may come from a pop that predates the history window, and the day's tally
     * still moves.
     *
     * @note The day's tally follows the word rather than the press: it is booked against the
     *       day the pop happened, and a word re-marked moves from one column to the other, so
     *       one word never counts in both. Pressing the verdict the word already carries moves
     *       nothing. A verdict on a pop that has aged out of the window has no day to book
     *       against and lands on the day of the mark.
     *
     * @param minute Local time of the mark; its date picks the daily bucket.
     * @param verdict "known" or "new".
     */
    void recordVerdict(const std::string& lemma, std::string minute, std::string verdict);

    /// @brief Add one response's token usage to its day.
    void recordUsage(const std::string& minute, long long promptTokens, long long completionTokens);

    /// @return The history, newest first.
    const std::vector<HistoryEntry>& history() const
    {
        return history_;
    }

    /// @return Every day's tally, keyed by local date as "YYYY-MM-DD".
    const std::map<std::string, DailyUsage>& daily() const
    {
        return daily_;
    }

private:
    /// @brief Write the typed state back into the document, for KnownStore::save() to persist.
    void writeBack();

    nlohmann::json& doc_;
    std::vector<HistoryEntry> history_; ///< Newest first, same order as the document.
    std::map<std::string, DailyUsage> daily_;
};

}
