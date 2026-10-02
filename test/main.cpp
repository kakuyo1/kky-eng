/**
 * @file main.cpp
 * @brief lens_test: the self-check entry point. Built on the host only, never shipped.
 *
 *   --filter       Offline self-check: the sample corpus, a KnownStore round trip, and the
 *                  network-free LlmClient helpers. No network, no API key, CI-safe.
 *   --smoke [word] Hits the real model once (default word: ubiquitous). Needs a key, costs
 *                  money, and its verdict is read by a human, so it never runs in CI.
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

/// @brief Offline check 1/3: replay test/eval_corpus.json through FilterCore.
/// @note Corpus fields are documented in PHASE1.md section 4.5. Change behaviour by
///       changing the corpus first; only touch src/ once --filter goes red.
void runCorpus() {
    LENS_INFO("--- corpus ---");
    const fs::path path = fs::path(LENS_SOURCE_DIR) / "test" / "eval_corpus.json";
    std::ifstream in(path);
    if (!in) {
        CHECK(false, "打不开样例集 " + path.string());
        return;
    }
    nlohmann::json corpus = nlohmann::json::parse(in, nullptr, false);
    if (corpus.is_discarded() || !corpus.is_array()) {
        CHECK(false, "样例集不是合法 JSON 数组 " + path.string());
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
            CHECK(false, where + "\n     抛异常: " + e.what());
            continue;
        }
        CHECK(surfaces(got) == want,
              where + "\n     text:   " + text + "\n     期望:  " + join(want) + "\n     实际:  " +
                  join(surfaces(got)));

        if (item.contains("expectLemmas")) {
            const auto wantLemmas = item.at("expectLemmas").get<std::vector<std::string>>();
            std::vector<std::string> gotLemmas;
            gotLemmas.reserve(got.size());
            for (const auto& c : got) gotLemmas.push_back(c.lemma);
            CHECK(gotLemmas == wantLemmas,
                  where + "\n     期望词根:  " + join(wantLemmas) + "\n     实际词根:  " + join(gotLemmas));
        }
    }
    std::printf("样例集：%zu 段\n", i);
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
        CHECK(!store.isKnown("ubiquitous"), "新建 store 里未标记的词不应算已会");
        CHECK(store.level() == 2, "档位默认应为 CET-4（2）");
        CHECK(store.explanationLang() == "en", "解释语言默认应为 en");

        store.mark("ubiquitous", true);
        store.mark("resilience", false);   // a new word
        CHECK(store.isKnown("ubiquitous"), "mark(已会) 后 isKnown 应为真");
        CHECK(!store.isKnown("resilience"), "mark(新词) 不应算已会");
        CHECK(store.known().count("ubiquitous") == 1, "known() 应含已会词");
        CHECK(store.known().count("resilience") == 0, "known() 不应含新词");

        bool rejected = false;
        try {
            store.setLevel(99);
        } catch (const std::out_of_range&) {
            rejected = true;
        }
        CHECK(rejected, "越界档位应被拒绝，而不是静默写坏配置");

        store.setLevel(4);
        store.setExplanationLang("zh");
        store.cachePut("ubiquitous", {"existing everywhere", "无处不在的", "Phones are ubiquitous."});
        store.save();
    }

    {
        auto store = lens::core::KnownStore::load(path);
        CHECK(store.isKnown("ubiquitous"), "reload 后已会词应保留");
        CHECK(!store.isKnown("resilience"), "reload 后新词仍不应算已会");
        CHECK(store.level() == 4, "reload 后档位应保留");
        CHECK(store.explanationLang() == "zh", "reload 后解释语言应保留");

        const auto hit = store.cacheGet("ubiquitous");
        CHECK(hit.has_value(), "reload 后缓存应命中");
        if (hit) CHECK(hit->zh == "无处不在的", "缓存中文释义应保留");

        store.setExplanationLang("en");
        CHECK(!store.cacheGet("ubiquitous").has_value(), "缓存应按解释语言分键");

        std::ifstream in(path);
        const auto doc = nlohmann::json::parse(in, nullptr, false);
        CHECK(!doc.is_discarded(), "保存后的文件应是合法 JSON");
        if (doc.is_object()) {
            CHECK(doc.value("API-KEY", std::string()) == "sk-selftest", "save 必须保留无关键 API-KEY");
            CHECK(doc.value("URL", std::string()) == "https://example.invalid", "save 必须保留无关键 URL");
        }
    }

    fs::remove(path);
    std::printf("存储往返：完成\n");
}

/// @brief Offline check 3/3: the network-free LlmClient helpers.
///
/// The request-body cases pin down the DeepSeek constraints verified on 2026-10-02, so a
/// later edit cannot quietly drift back to the provider defaults. The response cases treat
/// the payload as untrusted: anything missing, extra, misspelled, or truncated must fail
/// the whole batch.
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
              std::string("脱敏 ") + c.in + "\n     期望: " + c.want +
                  "\n     实际: " + got.toStdString());
    }

    // Request body: pin the DeepSeek constraints so they cannot drift back to defaults.
    const Config cfg{QUrl("https://api.deepseek.com"), "sk-not-a-real-key", "deepseek-flash"};
    const QStringList words{"ubiquitous", "resilience"};
    for (const auto lang : {"en", "zh"}) {
        const auto doc = QJsonDocument::fromJson(buildRequestBody(cfg, words, lang));
        CHECK(doc.isObject(), "请求体应是合法 JSON 对象");
        const auto root = doc.object();
        CHECK(root.value("model").toString() == cfg.model, "请求体 model 应取自 Config");
        CHECK(root.value("stream").toBool() == false, "stream 应为 false");
        CHECK(root.value("response_format").toObject().value("type").toString() == "json_object",
              "response_format 应为 json_object");
        CHECK(root.value("thinking").toObject().value("type").toString() == "disabled",
              "必须显式关闭思考模式（DeepSeek 默认开启，开着会白花 reasoning token）");
        CHECK(root.value("max_tokens").toInt() > 0, "max_tokens 必须显式设，防 JSON 被截断");

        const auto msgs = root.value("messages").toArray();
        CHECK(msgs.size() == 2, "messages 应是 system + user 两条");
        if (msgs.size() == 2) {
            const auto sys = msgs.at(0).toObject().value("content").toString();
            const auto usr = msgs.at(1).toObject().value("content").toString();
            CHECK(msgs.at(0).toObject().value("role").toString() == "system", "首条 role=system");
            CHECK(msgs.at(1).toObject().value("role").toString() == "user", "次条 role=user");
            CHECK(sys.contains("json", Qt::CaseInsensitive),
                  "prompt 必须含 json 字样——json_object 的硬性要求，缺了会一路吐空白到 max_tokens");
            CHECK(sys.contains("\"results\""), "prompt 必须给出 JSON 格式示例");
            CHECK(usr.contains("ubiquitous") && usr.contains("resilience"), "user 消息应含全部待查词");
            CHECK(sys.contains(QString::fromUtf8("例句用中文")) ==
                      (QString(lang) == QStringLiteral("zh")),
                  "解释语言应切换提示词末句");
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
        const auto r = parseExplanations(envelope(good), want);
        CHECK(okOf(r), "合法响应应通过：" + errOf(r).toStdString());
        if (const auto* v = std::get_if<QVector<WordExplanation>>(&r)) {
            CHECK(v->size() == 1 && v->at(0).zh == QString::fromUtf8("无处不在的"),
                  "应带回中文释义");
        }
    }

    {   // The example is not shown in phase 1, so an empty one must not sink the batch.
        const auto r = parseExplanations(
            envelope(results(
                QStringLiteral(R"({"word":"ubiquitous","en":"x","zh":"y","example":""})"))),
            want);
        CHECK(okOf(r), "example 为空不应整体失败：" + errOf(r).toStdString());
    }

    const struct { const char* what; QByteArray body; } bad[] = {
        {"finish_reason=length（JSON 被截断）", envelope(good, QStringLiteral("length"))},
        {"content 不是 JSON", envelope(QStringLiteral("not json at all"))},
        {"缺 results 键", envelope(QStringLiteral("{}"))},
        {"字段缺失（无 zh）",
         envelope(results(QStringLiteral(R"({"word":"ubiquitous","en":"x","example":"y"})")))},
        {"字段缺失（无 example 键）",
         envelope(results(QStringLiteral(R"({"word":"ubiquitous","en":"x","zh":"y"})")))},
        {"字段为空（无 en）",
         envelope(results(QStringLiteral(R"({"word":"ubiquitous","en":"","zh":"y","example":"z"})")))},
        {"回显错词",
         envelope(results(QStringLiteral(R"({"word":"ubiquitos","en":"x","zh":"y","example":"z"})")))},
        {"漏词", envelope(results(QString()))},
        {"多余词",
         envelope(results(QStringLiteral(
             R"({"word":"ubiquitous","en":"x","zh":"y","example":"z"},{"word":"extra","en":"x","zh":"y","example":"z"})")))},
    };
    for (const auto& b : bad) {
        const auto r = parseExplanations(b.body, want);
        CHECK(!okOf(r), std::string("应整体失败：") + b.what);
    }

    std::printf("LLM 纯函数接缝：完成\n");
}

/// @brief Install logging against the repository's logs/ directory, whatever the cwd is,
///        and route Qt's own messages into the same logger.
void initLogging(spdlog::level::level_enum level = lens::log::kDefaultLevel) {
    lens::log::init(level, fs::path(LENS_SOURCE_DIR) / "logs");
    lens::log::installQtMessageHandler();
}

/// @brief Run every offline check. Requires the word list to be loaded first.
/// @return 0 when everything passed, 1 otherwise.
int runFilter() {
    LENS_INFO("=== offline self-check ===");
    lens::core::loadWordlist(fs::path(LENS_SOURCE_DIR) / "data" / "wordlist.txt");
    runCorpus();
    runStore();
    runLlmPure();
    if (g_fail == 0) {
        std::printf("离线自检通过\n");
        LENS_INFO("offline self-check passed");
        return 0;
    }
    std::printf("离线自检失败 %d 项\n", g_fail);
    LENS_ERROR("offline self-check failed: {} assertion(s)", g_fail);
    return 1;
}

/// @brief Smoke test: send one word to the real model and print what comes back.
///
/// Needs a key and spends money, so it is run by hand and never in CI. The key is read into
/// memory and never printed. DEV_SEND_CONFIRM is an app-level switch; typing this command is
/// itself the confirmation, which is why LlmClient knows nothing about that flag.
///
/// @param word Word to look up.
/// @return 0 on success, 2 for a configuration problem, 3 when the request failed.
int runSmoke(const std::string& word) {
    const fs::path path = fs::path(LENS_SOURCE_DIR) / "settings.local.json";
    std::ifstream in(path);
    if (!in) {
        std::printf("打不开配置 %s\n", path.string().c_str());
        LENS_ERROR("smoke: cannot open '{}'", path.string());
        return 2;
    }
    const auto doc = nlohmann::json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        std::printf("配置不是合法 JSON 对象\n");
        LENS_ERROR("smoke: '{}' is not a JSON object", path.string());
        return 2;
    }

    const std::string url = doc.value("URL", std::string());
    const std::string model = doc.value("MODEL", std::string());
    const std::string key = doc.value("API-KEY", std::string());
    if (url.empty() || model.empty() || key.empty()) {
        std::printf("配置缺 URL / MODEL / API-KEY（密钥是否为空不作细节说明）\n");
        LENS_ERROR("smoke: URL / MODEL / API-KEY missing from '{}'", path.string());
        return 2;
    }

    std::printf("冒烟：%s @ %s（密钥已读入，不打印）\n", model.c_str(), url.c_str());
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
        std::printf("失败：%s\n", message.toStdString().c_str());
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
    std::printf("用法：lens_test --filter | --smoke [单词]\n");
    return 2;
}
