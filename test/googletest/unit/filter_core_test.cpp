/**
 * @file filter_core_test.cpp
 * @brief FilterCore and KnownStore, offline: no network, no API key, CI-safe.
 *
 * The corpus half replays test/eval_corpus.json, whose fields PHASE1.md section 4.5
 * documents. Change behaviour by changing the corpus first; touch src/ once this goes red.
 */

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/filter_core.h"
#include "core/known_store.h"
#include "support.h"

namespace {

using lens::core::Candidate;
using lens::core::KnownStore;

// TEST_F pastes the fixture name into a class definition, so it has to be unqualified.
using lens::test::CoreTest;

/// @return Just the surface forms, which is what the corpus asserts on.
std::vector<std::string> surfaces(const std::vector<Candidate>& candidates)
{
    std::vector<std::string> out;
    out.reserve(candidates.size());
    for (const auto& candidate : candidates)
        out.push_back(candidate.surface);
    return out;
}

/// @return Just the lemmas, for the corpus entries that spell out an expected reduction.
std::vector<std::string> lemmas(const std::vector<Candidate>& candidates)
{
    std::vector<std::string> out;
    out.reserve(candidates.size());
    for (const auto& candidate : candidates)
        out.push_back(candidate.lemma);
    return out;
}

nlohmann::json readCorpus()
{
    std::ifstream in(lens::test::sourceDir() / "test" / "eval_corpus.json");
    EXPECT_TRUE(in) << "cannot open the corpus";
    if (!in) return {};

    nlohmann::json corpus = nlohmann::json::parse(in, nullptr, false);
    EXPECT_FALSE(corpus.is_discarded()) << "the corpus is not valid JSON";
    EXPECT_TRUE(corpus.is_array()) << "the corpus is not a JSON array";
    return corpus.is_array() ? corpus : nlohmann::json{};
}

/// @brief A KnownStore test on a fresh temp file, seeded with keys the store does not own.
struct KnownStoreTest : ::testing::Test {
protected:
    void SetUp() override
    {
        path = std::filesystem::temp_directory_path() / "lens_known_store_test.json";
        std::filesystem::remove(path);

        // Unrelated keys, so a test can prove save() carries through what it never read.
        std::ofstream out(path);
        out << R"({"API-KEY":"sk-selftest","URL":"https://example.invalid"})";
    }

    void TearDown() override
    {
        std::filesystem::remove(path);
    }

    /// @brief Mark a known and a new word, move off the defaults, cache one entry, persist.
    void writeSeededStore()
    {
        auto store = KnownStore::load(path);
        store.mark("ubiquitous", true);
        store.mark("resilience", false);
        store.setLevel(4);
        store.setExplanationLang("zh");
        // A real Chinese definition, kept non-ASCII on purpose: this is what proves the JSON
        // round trip survives UTF-8, which an ASCII stand-in would not. The IPA stands in for
        // the same reason: a bubble has to be able to show the pronunciation from the cache.
        store.cachePut("ubiquitous", {"/juːˈbɪkwɪtəs/", "existing everywhere", "无处不在的"});
        store.save();
    }

    std::filesystem::path path;
};

} // namespace

TEST_F(CoreTest, ReplaysTheSampleCorpus)
{
    const nlohmann::json corpus = readCorpus();
    ASSERT_FALSE(corpus.empty());
    ASSERT_TRUE(corpus.is_array());

    std::size_t index = 0;
    for (const auto& item : corpus) {
        ++index;
        SCOPED_TRACE("corpus #" + std::to_string(index) + " " + item.value("note", std::string()));

        const std::string text = item.at("text").get<std::string>();
        const auto expect = item.at("expect").get<std::vector<std::string>>();
        const std::size_t minFreqRank = item.value("minFreqRank", std::size_t{0});

        std::unordered_set<std::string> known;
        for (const auto& word : item.value("known", std::vector<std::string>{}))
            known.insert(word);

        std::vector<Candidate> got;
        ASSERT_NO_THROW(got = lens::core::filterWords(text, known, minFreqRank))
            << "text: " << text;

        EXPECT_EQ(surfaces(got), expect) << "text: " << text;
        if (item.contains("expectLemmas"))
            EXPECT_EQ(lemmas(got), item.at("expectLemmas").get<std::vector<std::string>>())
                << "text: " << text;
    }
}

TEST_F(CoreTest, ClassifiesAWordSelectionAsTheWordChannel)
{
    const auto selection =
        lens::core::classifySelection("The ubiquitous nature of modern software makes resilience essential.", {}, 3000);

    EXPECT_EQ(selection.kind, lens::core::SelectionKind::Word);
    // The first candidate is the one the bar's request would carry, so a regression that
    // reordered the list shows up here rather than in a bubble naming the wrong word.
    ASSERT_FALSE(selection.candidates.empty());
    EXPECT_EQ(selection.candidates.front().surface, "ubiquitous");
}

TEST_F(CoreTest, ClassifiesEverythingElseAsASentence)
{
    // Glued junk and a stray number: nothing in it survives the filter, so the word channel
    // has nothing to send.
    EXPECT_EQ(lens::core::classifySelection("https://example.com/k8s report_final.txt 42 version2", {}, 0).kind,
              lens::core::SelectionKind::Sentence);
    // Nothing was selected at all.
    EXPECT_EQ(lens::core::classifySelection("", {}, 0).kind, lens::core::SelectionKind::Sentence);
    // A lone word the reader already knows: the filter drops it, and there is genuinely
    // nothing left to explain, so this is not a misclassification.
    const auto known = lens::core::classifySelection("ubiquitous", {"ubiquitous"}, 3000);
    EXPECT_EQ(known.kind, lens::core::SelectionKind::Sentence);
    EXPECT_TRUE(known.candidates.empty());
}

TEST_F(KnownStoreTest, FreshStoreHoldsTheDocumentedDefaults)
{
    const auto store = KnownStore::load(path);
    EXPECT_FALSE(store.isKnown("ubiquitous"));
    EXPECT_EQ(store.level(), 2); // CET-4
    EXPECT_EQ(store.explanationLang(), "en");
}

TEST_F(KnownStoreTest, MarkSeparatesKnownFromNewWords)
{
    auto store = KnownStore::load(path);
    store.mark("ubiquitous", true);
    store.mark("resilience", false);

    EXPECT_TRUE(store.isKnown("ubiquitous"));
    EXPECT_FALSE(store.isKnown("resilience"));
    EXPECT_EQ(store.known().count("ubiquitous"), 1);
    // A new word is still offered again, so it stays out of the set handed to filterWords.
    EXPECT_EQ(store.known().count("resilience"), 0);
}

TEST_F(KnownStoreTest, RejectsALevelOutsideTheRange)
{
    auto store = KnownStore::load(path);
    EXPECT_THROW(store.setLevel(99), std::out_of_range);
}

TEST_F(KnownStoreTest, SurvivesASaveAndReload)
{
    writeSeededStore();

    const auto store = KnownStore::load(path);
    EXPECT_TRUE(store.isKnown("ubiquitous"));
    EXPECT_FALSE(store.isKnown("resilience"));
    EXPECT_EQ(store.level(), 4);
    EXPECT_EQ(store.explanationLang(), "zh");

    const auto hit = store.cacheGet("ubiquitous");
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->ipa, "/juːˈbɪkwɪtəs/") << "the pronunciation did not survive the round trip";
    EXPECT_EQ(hit->en, "existing everywhere");
    EXPECT_EQ(hit->zh, "无处不在的");
}

/// The pronunciation is required of a cache entry, so a document written before the field
/// existed reads as empty rather than as a bubble that is missing a line. That miss is the
/// whole migration: the next lookup asks the model and writes a complete entry, and the entry
/// that could not be read goes out of the file on the save that follows.
TEST_F(KnownStoreTest, ACachedEntryWithoutAPronunciationIsNotAnEntry)
{
    std::ofstream(path) << R"({"cache":{"en":{"ubiquitous":{"en":"existing everywhere","zh":"无处不在的"}}}})";

    auto store = KnownStore::load(path);
    EXPECT_FALSE(store.cacheGet("ubiquitous").has_value());

    store.cachePut("resilience", {"/rɪˈzɪliəns/", "the capacity to recover", "恢复力"});
    store.save();

    std::ifstream in(path);
    ASSERT_TRUE(in);
    const auto doc = nlohmann::json::parse(in, nullptr, false);
    ASSERT_TRUE(doc.is_object());
    EXPECT_FALSE(doc["cache"]["en"].contains("ubiquitous")) << "the unreadable entry should not be written back";
    EXPECT_TRUE(doc["cache"]["en"].contains("resilience"));
}

TEST_F(KnownStoreTest, KeysTheCacheByExplanationLanguage)
{
    writeSeededStore();

    auto store = KnownStore::load(path);
    store.setExplanationLang("en");
    EXPECT_FALSE(store.cacheGet("ubiquitous").has_value());
}

TEST_F(KnownStoreTest, SaveWritesValidJsonAndKeepsKeysItDoesNotOwn)
{
    writeSeededStore();

    std::ifstream in(path);
    ASSERT_TRUE(in);
    const auto doc = nlohmann::json::parse(in, nullptr, false);
    ASSERT_FALSE(doc.is_discarded());
    ASSERT_TRUE(doc.is_object());
    EXPECT_EQ(doc.value("API-KEY", std::string()), "sk-selftest");
    EXPECT_EQ(doc.value("URL", std::string()), "https://example.invalid");
}
