#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

/**
 * @file known_store.h
 * @brief Local state that survives restarts: which words the reader knows, how they were
 *        marked, at what difficulty level, and the cached explanations.
 *
 * Everything lives in one JSON document -- settings.json in the reader's profile, the path
 * main.cpp works out -- that also carries the LLM API configuration. The file is loaded once
 * and rewritten on every change; keys this class does not recognise are passed through
 * untouched and never dropped.
 */

namespace lens::core {

/// @brief One cached sense, with an optional additional-language translation.
struct CachedSense {
    std::string en, zh, translation;
};

/// @brief Language and sense mode captured when an explanation request began.
struct CacheContext {
    std::string language;
    bool multipleSenses = false;
};

/// @brief A word's first definition and its ordered senses, in one cache context.
/// @note Persisted IPA must be present but may be empty; older entries without it miss.
struct WordCache {
    std::string ipa, en, zh;
    std::string translation;
    std::vector<CachedSense> senses;
};

/**
 * @brief Word marks, difficulty level, explanation language, and the explanation cache.
 *
 * @note Deliberately Qt-free: it parses JSON with nlohmann/json, so the core test targets can link it
 *       into an offline self-check with no Qt and no network.
 */
class KnownStore {
public:
    /**
     * @brief Read the store from disk.
     *
     * @param path Path to the shared settings document.
     * @return A store holding whatever the document contained.
     * @throws std::runtime_error If the file exists but cannot be parsed. A document that
     *         cannot be read is never silently reset: the first save afterwards would
     *         overwrite the key and the word marks. A missing file is a first run instead.
     */
    static KnownStore load(std::filesystem::path path);

    /// @return Whether the reader has marked this lemma as known.
    bool isKnown(const std::string& lemma) const;

    /// @brief Record a reader's verdict on a word.
    /// @param lemma   Dictionary form of the word.
    /// @param learned true for "already known" (never pop again), false for "new word"
    ///                (kept in the list and popped first next time).
    void mark(const std::string& lemma, bool learned);

    /// @return The known set to hand to FilterCore::filterWords, derived from the marks.
    const std::unordered_set<std::string>& known() const;

    /// @return Difficulty level, 0..7. The order is defined in GLOSSARY.md and UI.md 4.4.
    int level() const;
    /// @throws std::out_of_range If the level is outside 0..7.
    void setLevel(int);

    /// @return Explanation language code from the service catalog.
    std::string explanationLang() const;
    void setExplanationLang(std::string);

    /// @brief Look up a cached explanation for the current explanation language.
    /// @return The cached entry, or std::nullopt when nothing is cached.
    std::optional<WordCache> cacheGet(const std::string& lemma) const;
    void cachePut(const std::string& lemma, WordCache);

    /// @brief Look up or store an entry with the context captured when the request began.
    std::optional<WordCache> cacheGet(std::string const& lemma, CacheContext const& context) const;
    void cachePut(std::string const& lemma, WordCache entry, CacheContext const& context);

    /// @brief Remove a lemma's mark and every cached explanation context.
    /// @return True when a mark or cache entry was removed.
    bool removeLemma(std::string const& lemma);

    /**
     * @brief The document this store owns, for the section objects that persist beside it.
     *
     * StatsStore keeps its keys in the same document, and only one object may own it: two
     * copies loaded separately would each save their own stale view and the later save would
     * drop the earlier one's changes -- a word mark or the API key, silently. So the file has
     * one owner (this class) and the others write into its document; save() then persists
     * everything at once.
     *
     * @return The whole settings document, as loaded.
     */
    nlohmann::json& document()
    {
        return doc_;
    }

    /// @return The same document, read-only.
    const nlohmann::json& document() const
    {
        return doc_;
    }

    /// @brief Write the whole document back, preserving every unrecognised key.
    /// @throws std::runtime_error If the file cannot be written.
    void save() const;

private:
    std::filesystem::path path_;
    nlohmann::json doc_; ///< The document as loaded; the base for save().
    /// lemma -> known(true) / new word(false). Absent means never marked.
    std::unordered_map<std::string, bool> marks_;
    /// Derived view of marks_ holding only the true entries; feeds filterWords.
    std::unordered_set<std::string> known_;
    /// Keyed by explanation language + sense mode + lemma.
    std::unordered_map<std::string, WordCache> cache_;
    int level_        = 2;    ///< CET-4, the default difficulty level.
    std::string lang_ = "en"; ///< English by default.
};

}
