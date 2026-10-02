#include "core/known_store.h"
#include "core/log.h"

#include <fstream>
#include <stdexcept>

namespace lens::core {
namespace {

constexpr int kDefaultLevel = 2;   ///< CET-4.
/// Order is defined in UI.md 4.4: B1-B2 / C1-C2 / CET-4 / CET-6 / TEM-4 / TEM-8 / IELTS / TOEFL.
constexpr int kMaxLevel = 7;
/// Separator between language and lemma in a cache key. Cannot occur inside either.
constexpr char kSep = '\x1f';

std::string cacheKey(const std::string& lang, const std::string& lemma) {
    return lang + kSep + lemma;
}

}   // namespace

KnownStore KnownStore::load(std::filesystem::path path) {
    KnownStore store;
    store.path_ = std::move(path);
    LENS_TRACE("KnownStore::load: reading '{}'", store.path_.string());

    std::ifstream in(store.path_);
    if (!in) {
        store.doc_ = nlohmann::json::object();   // no file yet: first run
        LENS_INFO("settings not found at '{}'; starting from defaults", store.path_.string());
        return store;
    }

    store.doc_ = nlohmann::json::parse(in, nullptr, false);
    // The file is there but unreadable. Never silently reset it: the next save would
    // overwrite the reader's API key and word marks.
    if (store.doc_.is_discarded()) {
        LENS_ERROR("KnownStore::load: '{}' is not valid JSON; refusing to reset it",
                      store.path_.string());
        throw std::runtime_error("settings JSON 解析失败：" + store.path_.string());
    }
    if (!store.doc_.is_object()) {
        LENS_ERROR("KnownStore::load: '{}' does not hold a JSON object at the top level",
                      store.path_.string());
        throw std::runtime_error("settings JSON 顶层不是对象：" + store.path_.string());
    }

    const nlohmann::json& doc = store.doc_;

    if (doc.contains("level") && doc["level"].is_number_integer())
        store.level_ = doc["level"].get<int>();
    if (store.level_ < 0 || store.level_ > kMaxLevel) {
        LENS_WARN("KnownStore::load: level {} out of range; falling back to {}", store.level_,
                     kDefaultLevel);
        store.level_ = kDefaultLevel;
    }

    if (doc.contains("explanationLang") && doc["explanationLang"].is_string())
        store.lang_ = doc["explanationLang"].get<std::string>();

    if (doc.contains("known") && doc["known"].is_object()) {
        for (auto it = doc["known"].begin(); it != doc["known"].end(); ++it) {
            if (!it.value().is_boolean()) continue;
            const bool learned = it.value().get<bool>();
            store.marks_[it.key()] = learned;
            if (learned) store.known_.insert(it.key());
        }
    }

    if (doc.contains("cache") && doc["cache"].is_object()) {
        for (auto lang = doc["cache"].begin(); lang != doc["cache"].end(); ++lang) {
            if (!lang.value().is_object()) continue;
            for (auto entry = lang.value().begin(); entry != lang.value().end(); ++entry) {
                const nlohmann::json& e = entry.value();
                if (!e.is_object()) continue;
                store.cache_[cacheKey(lang.key(), entry.key())] =
                    WordCache{e.value("en", std::string()),
                              e.value("zh", std::string()),
                              e.value("example", std::string())};
            }
        }
    }

    LENS_INFO("settings loaded: level={} lang={} known={} cached={}", store.level_, store.lang_,
                 store.known_.size(), store.cache_.size());
    return store;
}

bool KnownStore::isKnown(const std::string& lemma) const { return known_.count(lemma) != 0; }

void KnownStore::mark(const std::string& lemma, bool learned) {
    marks_[lemma] = learned;
    if (learned)
        known_.insert(lemma);
    else
        known_.erase(lemma);
    LENS_TRACE("KnownStore::mark: '{}' -> {}", lemma, learned ? "known" : "new word");
}

const std::unordered_set<std::string>& KnownStore::known() const { return known_; }

int KnownStore::level() const { return level_; }

void KnownStore::setLevel(int level) {
    if (level < 0 || level > kMaxLevel) {
        LENS_ERROR("KnownStore::setLevel: level {} is outside 0..{}", level, kMaxLevel);
        throw std::out_of_range("档位越界：" + std::to_string(level));
    }
    level_ = level;
    LENS_TRACE("KnownStore::setLevel: {}", level);
}

std::string KnownStore::explanationLang() const { return lang_; }

void KnownStore::setExplanationLang(std::string lang) {
    LENS_TRACE("KnownStore::setExplanationLang: '{}' -> '{}'", lang_, lang);
    lang_ = std::move(lang);
}

std::optional<WordCache> KnownStore::cacheGet(const std::string& lemma) const {
    const auto it = cache_.find(cacheKey(lang_, lemma));
    if (it == cache_.end()) {
        LENS_TRACE("KnownStore::cacheGet: miss for '{}' ({})", lemma, lang_);
        return std::nullopt;
    }
    LENS_TRACE("KnownStore::cacheGet: hit for '{}' ({})", lemma, lang_);
    return it->second;
}

void KnownStore::cachePut(const std::string& lemma, WordCache entry) {
    LENS_TRACE("KnownStore::cachePut: '{}' ({})", lemma, lang_);
    cache_[cacheKey(lang_, lemma)] = std::move(entry);
}

void KnownStore::save() const {
    // Start from the document as loaded and overwrite only our own keys; everything else
    // (API-KEY, URL, settings added later) is carried through untouched.
    nlohmann::json doc = doc_.is_object() ? doc_ : nlohmann::json::object();
    doc["level"] = level_;
    doc["explanationLang"] = lang_;

    nlohmann::json known = nlohmann::json::object();
    for (const auto& [lemma, learned] : marks_) known[lemma] = learned;   // true: known
    doc["known"] = std::move(known);

    nlohmann::json cache = nlohmann::json::object();
    for (const auto& [key, entry] : cache_) {
        const std::size_t sep = key.find(kSep);
        cache[key.substr(0, sep)][key.substr(sep + 1)] = {
            {"en", entry.en}, {"zh", entry.zh}, {"example", entry.example}};
    }
    doc["cache"] = std::move(cache);

    std::ofstream out(path_, std::ios::trunc);
    if (!out) {
        LENS_ERROR("KnownStore::save: cannot write '{}'", path_.string());
        throw std::runtime_error("settings 写不进去：" + path_.string());
    }
    out << doc.dump(2) << '\n';
    LENS_INFO("settings saved: level={} lang={} marked={} cached={}", level_, lang_,
                 marks_.size(), cache_.size());
}

}
