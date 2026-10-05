/**
 * @file filter_core_test.cpp
 * @brief FilterCore and KnownStore, offline: no network, no API key, CI-safe.
 *
 * The corpus half replays test/eval_corpus.json.
 * Change behaviour by changing the corpus first; touch src/ once this goes red.
 */

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
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

    std::size_t mismatched = 0;
    std::size_t index      = 0;
    for (const auto& item : corpus) {
        ++index;
        SCOPED_TRACE("corpus #" + std::to_string(index) + " " + item.value("note", std::string()));

        const std::string text        = item.at("text").get<std::string>();
        const auto expect             = item.at("expect").get<std::vector<std::string>>();
        const std::size_t minFreqRank = item.value("minFreqRank", std::size_t{0});
        const std::string expectKind  = item.value("expectKind", std::string());

        std::unordered_set<std::string> known;
        for (const auto& word : item.value("known", std::vector<std::string>{}))
            known.insert(word);

        // The kind comes from classifySelection: the shape of the selection decides the channel.
        // The candidate list comes from filterWords directly, because a sentence now rides the
        // sentence channel while still exercising the word pipeline that fills `expect`.
        lens::core::Selection got;
        ASSERT_NO_THROW(got = lens::core::classifySelection(text, known, minFreqRank))
            << "text: " << text;
        const std::string kind = got.kind == lens::core::SelectionKind::Word
                                     ? "Word"
                                 : got.kind == lens::core::SelectionKind::Entity ? "Entity"
                                                                                 : "Sentence";
        EXPECT_EQ(kind, expectKind) << "text: " << text;
        bool ok = kind == expectKind;

        if (expectKind == "Entity") {
            // An entity phrase is not a word candidate list; filterWords is not its contract.
            if (!ok) ++mismatched;
            continue;
        }

        std::vector<Candidate> words;
        ASSERT_NO_THROW(words = lens::core::filterWords(text, known, minFreqRank)) << "text: " << text;
        EXPECT_EQ(surfaces(words), expect) << "text: " << text;
        ok = ok && surfaces(words) == expect;
        if (item.contains("expectLemmas")) {
            const auto wantLemmas = item.at("expectLemmas").get<std::vector<std::string>>();
            EXPECT_EQ(lemmas(words), wantLemmas) << "text: " << text;
            ok = ok && lemmas(words) == wantLemmas;
        }
        if (!ok) ++mismatched;
    }

    // Also the measurement: how many excerpts the pipeline gets wrong. Printed because a red
    // run is otherwise a wall of failures with no total, and docs/metrics reads this line.
    std::cout << "\ncorpus: " << corpus.size() << " entries, " << mismatched << " mismatched\n";
}

TEST_F(CoreTest, RoutesASentenceToTheSentenceChannelButStillFiltersItsWords)
{
    constexpr const char* text = "The ubiquitous nature of modern software makes resilience essential.";

    // The channel is the shape of the selection: a sentence is a sentence, so the action bar
    // translates or explains it whole instead of explaining one word.
    EXPECT_EQ(lens::core::classifySelection(text, {}, 3000).kind, lens::core::SelectionKind::Sentence);

    // filterWords still produces the candidate list the corpus pins, in first-appearance order,
    // with the in-band article marked rather than dropped (TODO.md item 0) -- which is why the
    // list opens with the article.
    const auto words = lens::core::filterWords(text, {}, 3000);
    ASSERT_FALSE(words.empty());
    EXPECT_EQ(words.front().surface, "the");
    EXPECT_EQ(words.front().state, lens::core::CandidateState::Mastered);

    // The candidate still fresh to the reader is what a word request would carry; a regression
    // that reordered the list shows up here rather than in a bubble naming the wrong word.
    const auto fresh = std::find_if(words.begin(), words.end(), [](const Candidate& candidate) {
        return candidate.state == lens::core::CandidateState::New;
    });
    ASSERT_NE(fresh, words.end());
    EXPECT_EQ(fresh->surface, "ubiquitous");
}

TEST_F(CoreTest, RoutesALoneTokenByItsPresenceInTheWordList)
{
    // A lone token the dictionary knows is a word to look up, whatever its case.
    const struct {
        const char* text;
        const char* surface;
    } words[] = {{"am", "am"}, {"NASA", "nasa"}, {"the", "the"}, {"resilience", "resilience"}};
    for (const auto& item : words) {
        SCOPED_TRACE(item.text);
        const auto selection = lens::core::classifySelection(item.text, {}, 3000);
        EXPECT_EQ(selection.kind, lens::core::SelectionKind::Word);
        ASSERT_EQ(selection.candidates.size(), 1u);
        EXPECT_EQ(selection.candidates.front().surface, item.surface);
    }

    // A lone token the dictionary does not know is a name, not a word: QML, Kubernetes.
    for (const char* text : {"QML", "qml", "kubernetes"}) {
        SCOPED_TRACE(text);
        const auto selection = lens::core::classifySelection(text, {}, 3000);
        EXPECT_EQ(selection.kind, lens::core::SelectionKind::Entity);
        EXPECT_TRUE(selection.candidates.empty());
    }

    // A lone dictionary word still reduces to its lemma.
    const auto reduced = lens::core::classifySelection("RUNNING", {}, 0);
    EXPECT_EQ(reduced.kind, lens::core::SelectionKind::Word);
    ASSERT_EQ(reduced.candidates.size(), 1u);
    EXPECT_EQ(reduced.candidates.front().lemma, "run");

    // One letter is under the floor, and a digit inside disqualifies (MP3).
    EXPECT_EQ(lens::core::classifySelection("a", {}, 3000).kind, lens::core::SelectionKind::Sentence);
    EXPECT_EQ(lens::core::classifySelection("MP3", {}, 3000).kind, lens::core::SelectionKind::Sentence);

    // The same term inside continuous prose makes the selection a sentence, and the gates still
    // keep it out of the candidate list.
    EXPECT_EQ(lens::core::classifySelection("the qml source", {}, 3000).kind, lens::core::SelectionKind::Sentence);
    for (const auto& candidate : lens::core::filterWords("the qml source", {}, 3000))
        EXPECT_NE(candidate.surface, "qml") << "a lone-selection token inside prose must stay skipped";
}

TEST_F(CoreTest, ClassifiesJunkAsASentence)
{
    // Glued junk and a stray number: nothing in it survives the filter, so the word channel
    // has nothing to send.
    EXPECT_EQ(lens::core::classifySelection("https://example.com/k8s report_final.txt 42 version2", {}, 0).kind,
              lens::core::SelectionKind::Sentence);
    // Nothing was selected at all.
    EXPECT_EQ(lens::core::classifySelection("", {}, 0).kind, lens::core::SelectionKind::Sentence);
    // A lone word the reader already knows is still a word: the mark annotates the candidate,
    // it does not take it away, because an explicit selection is a request to explain it --
    // the reader cannot be assumed to remember a word they marked (TODO.md item 0).
    const auto known = lens::core::classifySelection("ubiquitous", {"ubiquitous"}, 3000);
    EXPECT_EQ(known.kind, lens::core::SelectionKind::Word);
    ASSERT_EQ(known.candidates.size(), 1u);
    EXPECT_EQ(known.candidates.front().state, lens::core::CandidateState::Known);
    // A lone word inside the level's band is the same story from the other side.
    const auto mastered = lens::core::classifySelection("nature", {}, 3000);
    EXPECT_EQ(mastered.kind, lens::core::SelectionKind::Word);
    ASSERT_EQ(mastered.candidates.size(), 1u);
    EXPECT_EQ(mastered.candidates.front().state, lens::core::CandidateState::Mastered);
}

TEST_F(CoreTest, ClassifiesTitleCaseAndAcronymPhrasesAsEntities)
{
    const auto entity = lens::core::classifySelection("New York", {}, 0);
    EXPECT_EQ(entity.kind, lens::core::SelectionKind::Entity);
    EXPECT_TRUE(entity.candidates.empty());

    const auto anotherEntity = lens::core::classifySelection("United States", {}, 0);
    EXPECT_EQ(anotherEntity.kind, lens::core::SelectionKind::Entity);

    // An all-caps acronym phrase is a name too: QML API is an entity, not a two-word sentence.
    const auto acronym = lens::core::classifySelection("QML API", {}, 0);
    EXPECT_EQ(acronym.kind, lens::core::SelectionKind::Entity);
    EXPECT_TRUE(acronym.candidates.empty());
    EXPECT_EQ(lens::core::classifySelection("the QML API", {}, 0).kind, lens::core::SelectionKind::Entity);

    // A lowercase word that is not a connector keeps it a sentence.
    EXPECT_EQ(lens::core::classifySelection("QML API server", {}, 0).kind, lens::core::SelectionKind::Sentence);

    const auto singleToken = lens::core::classifySelection("London", {}, 0);
    EXPECT_NE(singleToken.kind, lens::core::SelectionKind::Entity);

    const auto mixedSentence = lens::core::classifySelection("I visited New York", {}, 0);
    EXPECT_NE(mixedSentence.kind, lens::core::SelectionKind::Entity);
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

/// An entry written before the ipa field existed has no key, so it reads as empty rather than as
/// a bubble that is missing a line. That miss is the whole migration: the next lookup asks the
/// model and writes a complete entry, and the entry that could not be read goes out of the file
/// on the save that follows. An empty ipa is a legitimate entry now (an acronym has none), so
/// only the missing key is dropped.
TEST_F(KnownStoreTest, ACachedEntryFromBeforeTheIPAFieldIsNotAnEntry)
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
