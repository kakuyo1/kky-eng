#include "core/known_store.h"
#include "util/log.h"

#include <fstream>
#include <stdexcept>

namespace lens::core {
namespace {

constexpr int kDefaultLevel = 2; ///< CET-4.
/// Order is defined in UI.md 4.4: B1-B2 / C1-C2 / CET-4 / CET-6 / TEM-4 / TEM-8 / IELTS / TOEFL.
constexpr int kMaxLevel = 7;
/// Separator between language and lemma in a cache key. Cannot occur inside either.
constexpr char kSep = '\x1f';

std::string cacheKey(CacheContext const& context, std::string const& lemma)
{
    return context.language + kSep + (context.multipleSenses ? "multiple" : "single") + kSep + lemma;
}

std::optional<WordCache> readCache(nlohmann::json const& entry, std::string const& language)
{
    if (not entry.is_object() or not entry.contains("ipa") or not entry["ipa"].is_string()) return std::nullopt;
    for (auto const* field : {"en", "zh"})
        if (not entry.contains(field) or not entry[field].is_string() or entry[field].get<std::string>().empty())
            return std::nullopt;
    WordCache result{entry["ipa"].get<std::string>(), entry["en"].get<std::string>(), entry["zh"].get<std::string>()};
    if (entry.contains("translation")) {
        if (not entry["translation"].is_string()) return std::nullopt;
        result.translation = entry["translation"].get<std::string>();
    }
    // Optional, and absent from every entry written before the setting existed. An empty one is
    // kept as empty rather than failing the read: it says the model had no origin to give, not that
    // the entry is damaged.
    if (entry.contains("etymology")) {
        if (not entry["etymology"].is_string()) return std::nullopt;
        result.etymology = entry["etymology"].get<std::string>();
    }
    if (language != "en" and language != "zh" and result.translation.empty()) return std::nullopt;
    if (entry.contains("senses")) {
        if (not entry["senses"].is_array() or entry["senses"].empty()) return std::nullopt;
        for (auto const& sense : entry["senses"]) {
            if (not sense.is_object()) return std::nullopt;
            for (auto const* field : {"en", "zh"})
                if (not sense.contains(field) or not sense[field].is_string() or sense[field].get<std::string>().empty())
                    return std::nullopt;
            auto const translation = sense.find("translation");
            if (translation != sense.end() and not translation->is_string()) return std::nullopt;
            auto const text = translation == sense.end() ? std::string{} : translation->get<std::string>();
            if (language != "en" and language != "zh" and text.empty()) return std::nullopt;
            if (result.senses.size() < 3)
                result.senses.push_back({sense["en"].get<std::string>(), sense["zh"].get<std::string>(), text});
        }
        result.en          = result.senses.front().en;
        result.zh          = result.senses.front().zh;
        result.translation = result.senses.front().translation;
    }
    return result;
}

nlohmann::json writeCache(WordCache const& entry)
{
    nlohmann::json value{{"ipa", entry.ipa}, {"en", entry.en}, {"zh", entry.zh}};
    if (not entry.translation.empty()) value["translation"] = entry.translation;
    if (not entry.etymology.empty()) value["etymology"] = entry.etymology;
    if (not entry.senses.empty()) {
        value["senses"] = nlohmann::json::array();
        for (auto const& sense : entry.senses)
            value["senses"].push_back({{"en", sense.en}, {"zh", sense.zh}, {"translation", sense.translation}});
    }
    return value;
}

} // namespace

KnownStore KnownStore::load(std::filesystem::path path)
{
    KnownStore store;
    store.path_ = std::move(path);
    LENS_TRACE("KnownStore::load: reading '{}'", store.path_.string());

    std::ifstream in(store.path_);
    if (!in) {
        store.doc_ = nlohmann::json::object(); // no file yet: first run
        LENS_INFO("settings not found at '{}'; starting from defaults", store.path_.string());
        return store;
    }

    store.doc_ = nlohmann::json::parse(in, nullptr, false);
    // The file is there but unreadable. Never silently reset it: the next save would
    // overwrite the reader's API key and word marks.
    if (store.doc_.is_discarded()) {
        LENS_ERROR("KnownStore::load: '{}' is not valid JSON; refusing to reset it",
                   store.path_.string());
        throw std::runtime_error("Cannot parse the settings JSON: " + store.path_.string());
    }
    if (!store.doc_.is_object()) {
        LENS_ERROR("KnownStore::load: '{}' does not hold a JSON object at the top level",
                   store.path_.string());
        throw std::runtime_error("The settings JSON is not an object at the top level: " +
                                 store.path_.string());
    }

    const nlohmann::json& doc = store.doc_;

    if (doc.contains("level") && doc["level"].is_number_integer())
        store.level_ = doc["level"].get<int>();
    if (store.level_ < 0 || store.level_ > kMaxLevel) {
        LENS_WARN("KnownStore::load: level {} out of range; falling back to {}", store.level_, kDefaultLevel);
        store.level_ = kDefaultLevel;
    }

    if (doc.contains("explanationLang") && doc["explanationLang"].is_string())
        store.lang_ = doc["explanationLang"].get<std::string>();

    if (doc.contains("known") && doc["known"].is_object()) {
        for (auto it = doc["known"].begin(); it != doc["known"].end(); ++it) {
            if (!it.value().is_boolean()) continue;
            const bool learned     = it.value().get<bool>();
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
                // Legacy flat entries remain single-sense entries. Multiple mode never falls
                // back to them; it has its own nested entry under the same lemma.
                if (auto cached = readCache(e, lang.key()))
                    store.cache_[cacheKey({lang.key(), false}, entry.key())] = std::move(*cached);
                if (e.contains("multiple"))
                    if (auto cached = readCache(e["multiple"], lang.key()); cached and not cached->senses.empty())
                        store.cache_[cacheKey({lang.key(), true}, entry.key())] = std::move(*cached);
            }
        }
    }

    LENS_INFO("settings loaded: level={} lang={} known={} cached={}", store.level_, store.lang_, store.known_.size(), store.cache_.size());
    return store;
}

bool KnownStore::isKnown(const std::string& lemma) const
{
    return known_.count(lemma) != 0;
}

void KnownStore::mark(const std::string& lemma, bool learned)
{
    marks_[lemma] = learned;
    if (learned)
        known_.insert(lemma);
    else
        known_.erase(lemma);
    LENS_TRACE("KnownStore::mark: '{}' -> {}", lemma, learned ? "known" : "new word");
}

const std::unordered_set<std::string>& KnownStore::known() const
{
    return known_;
}

int KnownStore::level() const
{
    return level_;
}

void KnownStore::setLevel(int level)
{
    if (level < 0 || level > kMaxLevel) {
        LENS_ERROR("KnownStore::setLevel: level {} is outside 0..{}", level, kMaxLevel);
        throw std::out_of_range("Level out of range: " + std::to_string(level));
    }
    level_ = level;
    LENS_TRACE("KnownStore::setLevel: {}", level);
}

std::string KnownStore::explanationLang() const
{
    return lang_;
}

void KnownStore::setExplanationLang(std::string lang)
{
    LENS_TRACE("KnownStore::setExplanationLang: '{}' -> '{}'", lang_, lang);
    lang_ = std::move(lang);
}

std::optional<WordCache> KnownStore::cacheGet(const std::string& lemma) const
{
    return cacheGet(lemma, {lang_, false});
}

std::optional<WordCache> KnownStore::cacheGet(std::string const& lemma, CacheContext const& context) const
{
    const auto it = cache_.find(cacheKey(context, lemma));
    if (it == cache_.end()) {
        LENS_TRACE("KnownStore::cacheGet: miss for '{}' ({})", lemma, lang_);
        return std::nullopt;
    }
    LENS_TRACE("KnownStore::cacheGet: hit for '{}' ({})", lemma, lang_);
    return it->second;
}

void KnownStore::cachePut(const std::string& lemma, WordCache entry)
{
    cachePut(lemma, std::move(entry), {lang_, false});
}

void KnownStore::cachePut(std::string const& lemma, WordCache entry, CacheContext const& context)
{
    LENS_TRACE("KnownStore::cachePut: '{}' ({})", lemma, lang_);
    if (entry.senses.size() > (context.multipleSenses ? 3u : 1u))
        entry.senses.resize(context.multipleSenses ? 3u : 1u);
    cache_[cacheKey(context, lemma)] = std::move(entry);
}

bool KnownStore::removeLemma(std::string const& lemma)
{
    bool removed = marks_.erase(lemma) != 0;
    removed      = known_.erase(lemma) != 0 || removed;

    for (auto it = cache_.begin(); it != cache_.end();) {
        const auto separator = it->first.rfind(kSep);
        if (separator != std::string::npos && it->first.substr(separator + 1) == lemma) {
            it      = cache_.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    if (removed)
        LENS_INFO("removed local word state for '{}'", lemma);
    return removed;
}

void KnownStore::save() const
{
    // Start from the document as loaded and overwrite only our own keys; everything else
    // (API-KEY, URL, settings added later) is carried through untouched.
    nlohmann::json doc     = doc_.is_object() ? doc_ : nlohmann::json::object();
    doc["level"]           = level_;
    doc["explanationLang"] = lang_;

    nlohmann::json known = nlohmann::json::object();
    for (const auto& [lemma, learned] : marks_)
        known[lemma] = learned; // true: known
    doc["known"] = std::move(known);

    nlohmann::json cache = nlohmann::json::object();
    for (const auto& [key, entry] : cache_) {
        auto const languageEnd = key.find(kSep);
        auto const modeEnd     = key.find(kSep, languageEnd + 1);
        auto& word             = cache[key.substr(0, languageEnd)][key.substr(modeEnd + 1)];
        if (not word.is_object()) word = nlohmann::json::object();
        if (key.substr(languageEnd + 1, modeEnd - languageEnd - 1) == "multiple")
            word["multiple"] = writeCache(entry);
        else
            word.update(writeCache(entry));
    }
    doc["cache"] = std::move(cache);

    std::ofstream out(path_, std::ios::trunc);
    if (!out) {
        LENS_ERROR("KnownStore::save: cannot write '{}'", path_.string());
        throw std::runtime_error("Cannot write the settings file: " + path_.string());
    }
    out << doc.dump(2) << '\n';
    LENS_INFO("settings saved: level={} lang={} marked={} cached={}", level_, lang_, marks_.size(), cache_.size());
}

}
