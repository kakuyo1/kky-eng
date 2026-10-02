/**
 * @file main.cpp
 * @brief lens_test: the self-check entry point. Built on the host only, never shipped.
 *
 *   --filter       Offline self-check: the sample corpus, a KnownStore round trip, and the
 *                  network-free LlmClient helpers. No network, no API key, CI-safe.
 *   --smoke [word] Hits the real model once (default word: ubiquitous). Needs a key, costs
 *                  money, and its verdict is read by a human, so it never runs in CI.
 *
 * Messages here are developer diagnostics: plain English, never translated.
 */

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>
#include <nlohmann/json.hpp>

#include "core/filter_core.h"
#include "core/known_store.h"
#include "core/log.h"
#include "llm/llm_client.h"
#include "llm/llm_protocol.h"
#include "llm/llm_pure.h"
#include "llm/qt_log.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifndef LENS_SOURCE_DIR
#error "LENS_SOURCE_DIR is undefined; build through the CMake target lens_test"
#endif

namespace fs = std::filesystem;
using lens::core::Candidate;
using lens::llm::Channel;

namespace {

int g_fail = 0;

/// @brief Record one assertion. Failures are printed and mirrored to the error log.
void check(bool ok, const char* file, int line, const std::string& msg) {
    if (ok) return;
    std::printf("FAIL %s:%d  %s\n", file, line, msg.c_str());
    LENS_ERROR("FAIL {}:{}  {}", file, line, msg);
    ++g_fail;
}

#define CHECK(cond, msg) check((cond), __FILE__, __LINE__, (msg))

/// @return The strings joined with ", " and wrapped in brackets, for failure messages.
std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) {
        if (!s.empty()) s += ", ";
        s += x;
    }
    return "[" + s + "]";
}

/// @return Just the surface forms of the candidates, which is what the corpus asserts on.
std::vector<std::string> surfaces(const std::vector<Candidate>& cs) {
    std::vector<std::string> v;
    v.reserve(cs.size());
    for (const auto& c : cs) v.push_back(c.surface);
    return v;
}

/// @return The directory holding the LLM wire protocol files.
fs::path protocolDir() { return fs::path(LENS_SOURCE_DIR) / "data" / "llm"; }

/// @brief Load the wire protocol the checks and the smoke run exercise.
void loadProtocol() { lens::llm::loadLlmProtocol(Channel::Word, protocolDir()); }

/// @brief Offline check 1/3: replay test/eval_corpus.json through FilterCore.
/// @note Corpus fields are documented in PHASE1.md section 4.5. Change behaviour by
///       changing the corpus first; only touch src/ once --filter goes red.
void runCorpus() {
    LENS_INFO("--- corpus ---");
    const fs::path path = fs::path(LENS_SOURCE_DIR) / "test" / "eval_corpus.json";
    std::ifstream in(path);
    if (!in) {
        CHECK(false, "cannot open the corpus: " + path.string());
        return;
    }
    nlohmann::json corpus = nlohmann::json::parse(in, nullptr, false);
    if (corpus.is_discarded() || !corpus.is_array()) {
        CHECK(false, "the corpus is not a JSON array: " + path.string());
        return;
    }

    std::size_t i = 0;
    for (const auto& item : corpus) {
        ++i;
        const std::string where = "#" + std::to_string(i) + " " + item.value("note", std::string());
        const std::string text = item.at("text").get<std::string>();
        const std::vector<std::string> want = item.at("expect").get<std::vector<std::string>>();
        const std::size_t minFreqRank = item.value("minFreqRank", std::size_t{0});

        std::unordered_set<std::string> known;
        for (const auto& w : item.value("known", std::vector<std::string>{})) known.insert(w);

        std::vector<Candidate> got;
        try {
            got = lens::core::filterWords(text, known, minFreqRank);
        } catch (const std::exception& e) {
            CHECK(false, where + "\n     threw: " + e.what());
            continue;
        }
        CHECK(surfaces(got) == want,
              where + "\n     text:  " + text + "\n     want:  " + join(want) + "\n     got:   " +
                  join(surfaces(got)));

        if (item.contains("expectLemmas")) {
            const auto wantLemmas = item.at("expectLemmas").get<std::vector<std::string>>();
            std::vector<std::string> gotLemmas;
            gotLemmas.reserve(got.size());
            for (const auto& c : got) gotLemmas.push_back(c.lemma);
            CHECK(gotLemmas == wantLemmas,
                  where + "\n     want lemmas: " + join(wantLemmas) + "\n     got lemmas:  " +
                      join(gotLemmas));
        }
    }
    std::printf("corpus: %zu case(s)\n", i);
}

/// @brief Offline check 2/3: KnownStore load / mark / cache / save / reload on a temp file.
void runStore() {
    LENS_INFO("--- known store round trip ---");
    const fs::path path = fs::temp_directory_path() / "lens_known_store_selftest.json";
    fs::remove(path);

    // Seed the file with unrelated API keys so the test can prove save() does not drop
    // keys it does not recognise.
    {
        std::ofstream o(path);
        o << R"({"API-KEY":"sk-selftest","URL":"https://example.invalid"})";
    }

    {
        auto store = lens::core::KnownStore::load(path);
        CHECK(!store.isKnown("ubiquitous"), "an unmarked word is not known in a fresh store");
        CHECK(store.level() == 2, "the default level is CET-4 (2)");
        CHECK(store.explanationLang() == "en", "the default explanation language is en");

        store.mark("ubiquitous", true);
        store.mark("resilience", false);   // a new word
        CHECK(store.isKnown("ubiquitous"), "mark(known) makes isKnown true");
        CHECK(!store.isKnown("resilience"), "mark(new word) leaves isKnown false");
        CHECK(store.known().count("ubiquitous") == 1, "known() holds known words");
        CHECK(store.known().count("resilience") == 0, "known() excludes new words");

        bool rejected = false;
        try {
            store.setLevel(99);
        } catch (const std::out_of_range&) {
            rejected = true;
        }
        CHECK(rejected, "an out-of-range level is rejected rather than silently written");

        store.setLevel(4);
        store.setExplanationLang("zh");
        // A real Chinese definition, kept non-ASCII on purpose: this is what proves the
        // JSON round trip survives UTF-8, which an ASCII stand-in would not.
        store.cachePut("ubiquitous", {"existing everywhere", "无处不在的", "Phones are ubiquitous."});
        store.save();
    }

    {
        auto store = lens::core::KnownStore::load(path);
        CHECK(store.isKnown("ubiquitous"), "a known word survives reload");
        CHECK(!store.isKnown("resilience"), "a new word is still not known after reload");
        CHECK(store.level() == 4, "the level survives reload");
        CHECK(store.explanationLang() == "zh", "the explanation language survives reload");

        const auto hit = store.cacheGet("ubiquitous");
        CHECK(hit.has_value(), "the cache is hit after reload");
        if (hit) CHECK(hit->zh == "无处不在的", "the cached Chinese definition survives");

        store.setExplanationLang("en");
        CHECK(!store.cacheGet("ubiquitous").has_value(), "the cache is keyed by explanation language");

        std::ifstream in(path);
        const auto doc = nlohmann::json::parse(in, nullptr, false);
        CHECK(!doc.is_discarded(), "the saved file is valid JSON");
        if (doc.is_object()) {
            CHECK(doc.value("API-KEY", std::string()) == "sk-selftest",
                  "save preserves the unrelated API-KEY");
            CHECK(doc.value("URL", std::string()) == "https://example.invalid",
                  "save preserves the unrelated URL");
        }
    }

    fs::remove(path);
    std::printf("known store round trip: done\n");
}

/// @brief Offline check 3/3: the network-free LlmClient helpers.
///
/// The request-body cases pin down what data/llm/request.word.json must keep saying, so an
/// edit to that file that breaks the DeepSeek contract fails here rather than in a paid
/// round trip. The response cases treat the payload as untrusted: anything missing, extra,
/// misspelled, or truncated must fail the whole batch.
void runLlmPure() {
    LENS_INFO("--- llm helpers ---");
    using lens::llm::Config;
    using lens::llm::WordExplanation;
    using lens::llm::buildRequestBody;
    using lens::llm::maskSensitive;
    using lens::llm::parseExplanations;
    using LlmResult = std::variant<QVector<WordExplanation>, QString>;

    auto errOf = [](const LlmResult& r) {
        const auto* e = std::get_if<QString>(&r);
        return e ? *e : QString();
    };
    auto okOf = [](const LlmResult& r) {
        return std::holds_alternative<QVector<WordExplanation>>(r);
    };

    // Masking: email, URL, and long digit runs collapse; a plain word is untouched.
    const struct { const char* in; const char* want; } masks[] = {
        {"mail a.b+tag@example.com now", "mail <email> now"},
        {"see https://example.com/p?q=1#f end", "see <url> end"},
        {"call 13800138000 now", "call <num> now"},
        {"ubiquitous", "ubiquitous"},
    };
    for (const auto& c : masks) {
        const auto got = maskSensitive(QString::fromUtf8(c.in));
        CHECK(got == QString::fromUtf8(c.want),
              std::string("mask ") + c.in + "\n     want: " + c.want +
                  "\n     got:  " + got.toStdString());
    }

    // Request body: pin what the data file must keep saying.
    const Config cfg{QUrl("https://api.deepseek.com"), "sk-not-a-real-key", "deepseek-flash"};
    const QStringList words{"ubiquitous", "resilience"};
    for (const auto lang : {"en", "zh"}) {
        const auto doc =
            QJsonDocument::fromJson(buildRequestBody(cfg, Channel::Word, words, lang));
        CHECK(doc.isObject(), "the request body is a JSON object");
        const auto root = doc.object();
        CHECK(root.value("model").toString() == cfg.model, "the request body carries the model");
        CHECK(root.value("stream").toBool() == false, "stream is false");
        CHECK(root.value("response_format").toObject().value("type").toString() == "json_object",
              "response_format is json_object");
        CHECK(root.value("thinking").toObject().value("type").toString() == "disabled",
              "thinking is explicitly disabled, since DeepSeek defaults to it on");
        CHECK(root.value("max_tokens").toInt() > 0, "max_tokens is set, so JSON cannot be truncated");

        const auto msgs = root.value("messages").toArray();
        CHECK(msgs.size() == 2, "messages holds a system and a user entry");
        if (msgs.size() == 2) {
            const auto sys = msgs.at(0).toObject().value("content").toString();
            const auto usr = msgs.at(1).toObject().value("content").toString();
            CHECK(msgs.at(0).toObject().value("role").toString() == "system",
                  "the first message is the system prompt");
            CHECK(msgs.at(1).toObject().value("role").toString() == "user",
                  "the second message is the user payload");
            CHECK(sys.contains("json", Qt::CaseInsensitive),
                  "the prompt contains the word json, which json_object requires");
            CHECK(sys.contains("\"results\""), "the prompt shows a JSON format example");
            CHECK(usr.contains("ubiquitous") && usr.contains("resilience"),
                  "the user message carries every requested item");
            CHECK(sys.contains(QStringLiteral("in Chinese")) ==
                      (QString(lang) == QStringLiteral("zh")),
                  "the explanation language switches the prompt's closing line");
        }
    }

    // Response validation: untrusted data. Missing, extra, misspelled, or truncated all
    // fail the batch as a whole.
    auto envelope = [](const QString& content, const QString& finish = QStringLiteral("stop")) {
        return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{
                                                        {"finish_reason", finish},
                                                        {"message", QJsonObject{{"content", content}}}}}}})
            .toJson(QJsonDocument::Compact);
    };
    auto results = [](const QString& items) { return QStringLiteral("{\"results\":[%1]}").arg(items); };

    const QStringList want{"ubiquitous"};
    const QString good = results(QStringLiteral(
        R"({"word":"ubiquitous","en":"existing everywhere","zh":"无处不在的","example":"Phones are ubiquitous."})"));

    {
        const auto r = parseExplanations(Channel::Word, envelope(good), want);
        CHECK(okOf(r), "a well-formed response is accepted: " + errOf(r).toStdString());
        if (const auto* v = std::get_if<QVector<WordExplanation>>(&r)) {
            CHECK(v->size() == 1 && v->at(0).zh == QString::fromUtf8("无处不在的"),
                  "the Chinese definition comes back");
        }
    }

    {   // The example is not shown in phase 1, so an empty one must not sink the batch.
        const auto r = parseExplanations(
            Channel::Word,
            envelope(results(
                QStringLiteral(R"({"word":"ubiquitous","en":"x","zh":"y","example":""})"))),
            want);
        CHECK(okOf(r), "an empty example does not fail the batch: " + errOf(r).toStdString());
    }

    const struct { const char* what; QByteArray body; } bad[] = {
        {"finish_reason=length, the JSON was cut off",
         envelope(good, QStringLiteral("length"))},
        {"content is not JSON", envelope(QStringLiteral("not json at all"))},
        {"no results key", envelope(QStringLiteral("{}"))},
        {"missing field (no zh)",
         envelope(results(QStringLiteral(R"({"word":"ubiquitous","en":"x","example":"y"})")))},
        {"missing field (no example key)",
         envelope(results(QStringLiteral(R"({"word":"ubiquitous","en":"x","zh":"y"})")))},
        {"empty field (no en)",
         envelope(results(QStringLiteral(R"({"word":"ubiquitous","en":"","zh":"y","example":"z"})")))},
        {"misspelled echo",
         envelope(results(QStringLiteral(R"({"word":"ubiquitos","en":"x","zh":"y","example":"z"})")))},
        {"missing echo", envelope(results(QString()))},
        {"extra echo",
         envelope(results(QStringLiteral(
             R"({"word":"ubiquitous","en":"x","zh":"y","example":"z"},{"word":"extra","en":"x","zh":"y","example":"z"})")))},
    };
    for (const auto& b : bad) {
        const auto r = parseExplanations(Channel::Word, b.body, want);
        CHECK(!okOf(r), std::string("rejected as a whole: ") + b.what);
    }

    std::printf("llm helpers: done\n");
}

/// @brief Install logging against the repository's logs/ directory, whatever the cwd is,
///        and route Qt's own messages into the same logger.
void initLogging(spdlog::level::level_enum level = lens::log::kDefaultLevel) {
    lens::log::init(level, fs::path(LENS_SOURCE_DIR) / "logs");
    lens::log::installQtMessageHandler();
}

/// @brief Run every offline check. Requires the word list and the protocol to be loaded.
/// @return 0 when everything passed, 1 otherwise.
int runFilter() {
    LENS_INFO("=== offline self-check ===");
    lens::core::loadWordlist(fs::path(LENS_SOURCE_DIR) / "data" / "wordlist.txt");
    lens::core::loadIrregulars(fs::path(LENS_SOURCE_DIR) / "data" / "irregulars.tsv");
    loadProtocol();
    runCorpus();
    runStore();
    runLlmPure();
    if (g_fail == 0) {
        std::printf("offline self-check passed\n");
        LENS_INFO("offline self-check passed");
        return 0;
    }
    std::printf("offline self-check failed: %d assertion(s)\n", g_fail);
    LENS_ERROR("offline self-check failed: {} assertion(s)", g_fail);
    return 1;
}

/// @brief Smoke test: send one word to the real model and print what comes back.
///
/// Needs a key and spends money, so it is run by hand and never in CI. The key is read into
/// memory and never printed. DEV_SEND_CONFIRM is an app-level switch; typing this command is
/// itself the confirmation, which is why LlmClient knows nothing about that flag.
///
/// @param word Item to look up.
/// @return 0 on success, 2 for a configuration problem, 3 when the request failed.
int runSmoke(const std::string& word) {
    const fs::path path = fs::path(LENS_SOURCE_DIR) / "settings.local.json";
    std::ifstream in(path);
    if (!in) {
        std::printf("cannot open the settings file %s\n", path.string().c_str());
        LENS_ERROR("smoke: cannot open '{}'", path.string());
        return 2;
    }
    const auto doc = nlohmann::json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        std::printf("the settings file is not a JSON object\n");
        LENS_ERROR("smoke: '{}' is not a JSON object", path.string());
        return 2;
    }

    const std::string url = doc.value("URL", std::string());
    const std::string model = doc.value("MODEL", std::string());
    const std::string key = doc.value("API-KEY", std::string());
    if (url.empty() || model.empty() || key.empty()) {
        std::printf("the settings file is missing URL / MODEL / API-KEY\n");
        LENS_ERROR("smoke: URL / MODEL / API-KEY missing from '{}'", path.string());
        return 2;
    }

    loadProtocol();
    std::printf("smoke: %s @ %s (key read into memory, never printed)\n", model.c_str(), url.c_str());
    LENS_INFO("smoke: {} @ {}", model, url);
    lens::llm::LlmClient client({QUrl(QString::fromStdString(url)), QString::fromStdString(key),
                                 QString::fromStdString(model)});

    int exitCode = 1;
    QObject::connect(&client, &lens::llm::LlmClient::batchFinished,
                     [&exitCode](QVector<lens::llm::WordExplanation> results) {
                         for (const auto& e : results)
                             std::printf("word:    %s\nen:      %s\nzh:      %s\nexample: %s\n",
                                         e.word.toStdString().c_str(), e.en.toStdString().c_str(),
                                         e.zh.toStdString().c_str(), e.example.toStdString().c_str());
                         exitCode = 0;
                         QCoreApplication::quit();
                     });
    QObject::connect(&client, &lens::llm::LlmClient::failed, [&exitCode](QString message) {
        std::printf("failed: %s\n", message.toStdString().c_str());
        LENS_ERROR("smoke failed: {}", message.toStdString());
        exitCode = 3;
        QCoreApplication::quit();
    });

    client.explainWords({QString::fromStdString(word)});
    QCoreApplication::exec();
    return exitCode;
}

}   // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    // --smoke needs an event loop; --filter does not, but constructing one is harmless.
    QCoreApplication app(argc, argv);
    std::vector<std::string> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--filter") {
            // Kept at info so the assertion output stays readable; raise it with
            // LENS_LOG_LEVEL=trace when the pipeline itself is under investigation.
            initLogging(spdlog::level::info);
            return runFilter();
        }
        if (args[i] == "--smoke") {
            initLogging();   // Debug default: trace
            const bool hasWord = i + 1 < args.size() && args[i + 1].rfind("--", 0) != 0;
            return runSmoke(hasWord ? args[i + 1] : "ubiquitous");
        }
    }
    std::printf("usage: lens_test --filter | --smoke [word]\n");
    return 2;
}
