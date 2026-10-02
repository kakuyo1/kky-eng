#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

/**
 * @file filter_core.h
 * @brief Turns a raw text excerpt into the short list of words worth explaining.
 *
 * The pipeline is deliberately dependency-free: no I/O and no Qt, so it can be unit
 * tested offline. Frequency ranks come from a static word list, and the caller injects
 * the known set and the difficulty cut-off.
 */

namespace lens::core {

/**
 * @brief Load the static word list into process-wide read-only state.
 *
 * @param path Word list file: one word per line, ordered by descending frequency.
 * @throws std::runtime_error If the file cannot be opened or contains no entries.
 *
 * @note Must run before lemmatize() or filterWords(). Those two throw std::logic_error
 *       rather than returning nothing, because calling them first is an initialisation
 *       order bug -- silently dropping every candidate would hide it.
 */
void loadWordlist(const std::filesystem::path& path);

/**
 * @brief Reduce a token to its dictionary form, so "running" and "ran" both become "run".
 *
 * @param token Word to reduce, case-insensitive.
 * @return The lemma. The known set, the explanation cache, and word list lookups are
 *         all keyed by lemma rather than by the form as written.
 * @throws std::logic_error If loadWordlist() has not run yet.
 */
std::string lemmatize(std::string_view token);

/// @brief A word worth explaining, in both the form seen and its dictionary form.
struct Candidate {
    std::string surface;   ///< The form as written, lower-cased (e.g. "running").
    std::string lemma;     ///< The dictionary form (e.g. "run").
};

/**
 * @brief Filter an excerpt down to the words the reader probably does not know.
 *
 * Pure pipeline with no I/O; the known set and the frequency cut-off are injected so the
 * module stays dependency-free and testable. Stages, in order: tokenise and hard-filter
 * -> skip all-caps -> lemmatise -> whitelist against the word list -> difficulty cut-off
 * -> skip known lemmas.
 *
 * @param text         Raw excerpt, typically pasted from the clipboard.
 * @param knownLemmas  Lemmas the reader has already marked as known.
 * @param minFreqRank  Difficulty cut-off: words at or above this rank count as mastered.
 *                     The level-to-rank mapping is not settled yet; see TODO.md.
 * @return Candidates in order of first appearance, de-duplicated by lemma.
 *         An empty vector means nothing needs explaining.
 * @throws std::logic_error If loadWordlist() has not run yet.
 */
std::vector<Candidate> filterWords(
    std::string_view text,
    const std::unordered_set<std::string>& knownLemmas,
    std::size_t minFreqRank);

}
