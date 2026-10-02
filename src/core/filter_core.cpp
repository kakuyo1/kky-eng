#include "core/filter_core.h"
#include "core/log.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace lens::core {
namespace {

using Table = std::unordered_map<std::string, std::size_t>;

/// Word to 1-based frequency rank. Empty means the word list has not been loaded.
Table g_rank;

bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlnum(char c) { return isLetter(c) || isDigit(c); }
bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}
char toLower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = toLower(c);
    return out;
}

bool inTable(const std::string& word) { return g_rank.contains(word); }

/// @return The word's rank, or 0 when the word is absent from the list.
std::size_t rankOf(const std::string& word) {
    const auto it = g_rank.find(word);
    return it == g_rank.end() ? 0 : it->second;
}

bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// Words that look inflected but are their own base form, and whose "stem" happens to be
/// another word in the list. Intercepted before any suffix rule can fire.
/// Only these two so far; add more when the sample set catches another.
const std::unordered_set<std::string>& keepAsIs() {
    static const std::unordered_set<std::string> words = {"news", "means"};
    return words;
}

/// Irregular form to base form. Only pairs no suffix rule could derive (went -> go,
/// children -> child, and the like).
const std::unordered_map<std::string, std::string>& irregulars() {
    static const std::unordered_map<std::string, std::string> table = {
        {"went", "go"},       {"gone", "go"},         {"ran", "run"},         {"came", "come"},
        {"took", "take"},     {"taken", "take"},      {"made", "make"},       {"said", "say"},
        {"gave", "give"},     {"given", "give"},      {"found", "find"},      {"thought", "think"},
        {"bought", "buy"},    {"brought", "bring"},   {"kept", "keep"},       {"held", "hold"},
        {"built", "build"},   {"sent", "send"},       {"spent", "spend"},     {"lost", "lose"},
        {"met", "meet"},      {"paid", "pay"},        {"told", "tell"},       {"felt", "feel"},
        {"left", "leave"},    {"meant", "mean"},      {"led", "lead"},        {"lay", "lie"},
        {"wrote", "write"},   {"written", "write"},   {"drove", "drive"},     {"driven", "drive"},
        {"rose", "rise"},     {"risen", "rise"},      {"grew", "grow"},       {"grown", "grow"},
        {"knew", "know"},     {"known", "know"},      {"threw", "throw"},     {"thrown", "throw"},
        {"drew", "draw"},     {"drawn", "draw"},      {"flew", "fly"},        {"flown", "fly"},
        {"began", "begin"},   {"begun", "begin"},     {"broke", "break"},     {"broken", "break"},
        {"chose", "choose"},  {"chosen", "choose"},   {"drank", "drink"},     {"drunk", "drink"},
        {"ate", "eat"},       {"eaten", "eat"},       {"fell", "fall"},       {"fallen", "fall"},
        {"forgot", "forget"}, {"forgotten", "forget"},{"hid", "hide"},        {"hidden", "hide"},
        {"sang", "sing"},     {"sung", "sing"},       {"sank", "sink"},       {"sunk", "sink"},
        {"sat", "sit"},       {"slept", "sleep"},     {"spoke", "speak"},     {"spoken", "speak"},
        {"stood", "stand"},   {"stole", "steal"},     {"stolen", "steal"},    {"swam", "swim"},
        {"swum", "swim"},     {"wore", "wear"},       {"worn", "wear"},       {"won", "win"},
        {"children", "child"},{"men", "man"},         {"women", "woman"},     {"feet", "foot"},
        {"teeth", "tooth"},   {"mice", "mouse"},      {"geese", "goose"},     {"people", "person"},
        {"better", "good"},   {"best", "good"},       {"worse", "bad"},       {"worst", "bad"},
    };
    return table;
}

/// Every candidate must already be a real word list entry. A stem that is not in the list
/// is meaningless and would only split "water" into "wat".
void pushIfInTable(std::vector<std::string>& out, std::string stem) {
    if (stem.size() >= 3 && inTable(stem)) out.push_back(std::move(stem));
}

/// Applies the doubled-consonant rule: "running" -> "runn" -> "run". Requires at least
/// 3 letters left over, which blocks the degenerate "inning" -> "in".
void pushDoubledFixed(std::vector<std::string>& out, const std::string& stem) {
    if (stem.size() < 4 || stem[stem.size() - 1] != stem[stem.size() - 2]) return;
    pushIfInTable(out, stem.substr(0, stem.size() - 1));
}

/// @note 'y' counts as a vowel here, which rescues rhythm / myth / gym.
bool hasVowel(const std::string& s) {
    for (char c : s)
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y') return true;
    return false;
}

}   // namespace

void loadWordlist(const std::filesystem::path& path) {
    const auto startedAt = std::chrono::steady_clock::now();
    spdlog::trace("loadWordlist: reading '{}'", path.string());

    std::ifstream in(path);
    if (!in) {
        spdlog::critical("loadWordlist: cannot open word list '{}'", path.string());
        throw std::runtime_error("词表打不开：" + path.string());
    }

    Table table;
    std::string line;
    std::size_t rank = 0;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        ++rank;
        table.emplace(lower(line), rank);   // duplicates keep their earliest rank
    }
    if (table.empty()) {
        spdlog::critical("loadWordlist: word list '{}' is empty", path.string());
        throw std::runtime_error("词表为空：" + path.string());
    }
    g_rank = std::move(table);

    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - startedAt)
                               .count();
    spdlog::info("wordlist loaded: {} entries from '{}' in {} ms", g_rank.size(),
                 path.string(), elapsedMs);
}

std::string lemmatize(std::string_view token) {
    if (g_rank.empty()) throw std::logic_error("lens::core::lemmatize：需先 loadWordlist");

    const std::string t = lower(token);
    if (keepAsIs().count(t) != 0) return t;

    // Irregular forms are decided outright rather than entered into the frequency
    // comparison below: children (451) outranks child (461), so picking by frequency would
    // pick children right back. What is wanted here is the explicitly listed base form.
    if (const auto it = irregulars().find(t); it != irregulars().end() && inTable(it->second))
        return it->second;

    // The base form and every stem the suffix rules produce become candidates; the winner
    // is whichever is most common in the word list (smallest frequency rank). The list
    // holds inflected and base forms alike, so frequency naturally favours the base form
    // (running 555 > run 314, increasing 7246 > increase 3568), while base forms whose
    // "stem" is not a word (water / under / offer) fall back to themselves.
    std::vector<std::string> cands;
    if (inTable(t)) cands.push_back(t);

    const std::size_t n = t.size();

    if (endsWith(t, "ies") && n > 4) pushIfInTable(cands, t.substr(0, n - 3) + "y");
    if (endsWith(t, "ied") && n > 4) pushIfInTable(cands, t.substr(0, n - 3) + "y");
    if (endsWith(t, "ier") && n > 5) pushIfInTable(cands, t.substr(0, n - 3) + "y");
    if (endsWith(t, "iest") && n > 6) pushIfInTable(cands, t.substr(0, n - 4) + "y");
    if (endsWith(t, "ily") && n > 5) pushIfInTable(cands, t.substr(0, n - 3) + "y");

    // -ing / -ed: first try restoring a dropped 'e' (making -> make, using -> use), then
    // the bare stem, then undoing a doubled consonant.
    // ponytail: only for words of 5+ letters. In 4-letter used/seed/feed the "stem + e"
    // candidate is usually a false reduction (seed -> see).
    const auto reduceVerbForm = [&](std::size_t cut) {
        if (n <= 4) return;
        const std::string stem = t.substr(0, n - cut);
        pushIfInTable(cands, stem + "e");
        pushIfInTable(cands, stem);
        pushDoubledFixed(cands, stem);
    };
    if (endsWith(t, "ing")) reduceVerbForm(3);
    if (endsWith(t, "ed")) reduceVerbForm(2);

    if (endsWith(t, "es") && n > 3) {
        pushIfInTable(cands, t.substr(0, n - 1));   // moves -> move
        pushIfInTable(cands, t.substr(0, n - 2));   // goes -> go
    } else if (endsWith(t, "s") && n > 3 && !endsWith(t, "ss")) {
        pushIfInTable(cands, t.substr(0, n - 1));
    }

    if (endsWith(t, "ly") && n > 5) {   // -ily was already tried above, mapping to y
        const std::string stem = t.substr(0, n - 2);
        pushIfInTable(cands, stem + "e");   // likely -> like
        pushIfInTable(cands, stem);         // quickly -> quick
    }

    // ponytail: comparatives -er/-est only fire at 6/7+ letters; at 5 letters the stems of
    // offer / under / other are all false reductions.
    // Cost: short-stem comparatives such as biggest -> big and nicer -> nice go
    // unreduced. The overlay then explains the inflected form, which is not wrong, but the
    // word is not merged with its lemma either.
    if (endsWith(t, "er") && n > 5) {
        const std::string stem = t.substr(0, n - 2);
        pushIfInTable(cands, stem + "e");   // larger -> large
        pushIfInTable(cands, stem);         // faster -> fast
        pushDoubledFixed(cands, stem);      // bigger -> big
    }
    if (endsWith(t, "est") && n > 6) {
        const std::string stem = t.substr(0, n - 3);
        pushIfInTable(cands, stem + "e");   // largest -> large
        pushIfInTable(cands, stem);         // fastest -> fast
        pushDoubledFixed(cands, stem);
    }

    if (cands.empty()) return t;   // not in the list; the caller's whitelist stage drops it

    const std::string* best = &cands.front();
    std::size_t bestRank = rankOf(*best);
    for (const auto& c : cands) {
        const std::size_t r = rankOf(c);
        if (r < bestRank) {
            bestRank = r;
            best = &c;
        }
    }

    if (*best != t) spdlog::trace("lemmatize: '{}' -> '{}' (rank {})", t, *best, bestRank);
    return *best;
}

std::vector<Candidate> filterWords(
    std::string_view text,
    const std::unordered_set<std::string>& knownLemmas,
    std::size_t minFreqRank) {
    if (g_rank.empty()) throw std::logic_error("lens::core::filterWords：需先 loadWordlist");

    std::vector<Candidate> out;
    std::unordered_set<std::string> seen;   // de-duplication keyed by lemma

    const std::size_t size = text.size();
    std::size_t i = 0;
    while (i < size) {
        while (i < size && isSpace(text[i])) ++i;
        const std::size_t start = i;
        while (i < size && !isSpace(text[i])) ++i;
        if (start == i) continue;

        // One whitespace-delimited run of the original text. Trim bytes that are neither
        // letters nor digits from both ends (punctuation, quotes, full-width CJK symbols);
        // if any non-letter byte survives inside, the run is glued junk (URL, email,
        // filename, version2, abbreviation) and is dropped whole. Digits stay inside the
        // token rather than being trimmed, otherwise version2 would be trimmed to version
        // and popped as a real word.
        std::size_t head = start;
        std::size_t tail = i;
        while (head < tail && !isAlnum(text[head])) ++head;
        while (tail > head && !isAlnum(text[tail - 1])) --tail;
        if (head == tail) continue;

        const std::string_view tok = text.substr(head, tail - head);
        bool glued = false;
        for (char c : tok)
            if (!isLetter(c)) { glued = true; break; }
        if (glued) continue;
        if (tok.size() < 3) continue;

        bool allCaps = true;
        for (char c : tok)
            if (c >= 'a' && c <= 'z') { allCaps = false; break; }
        if (allCaps) continue;   // THE / NASA / OK: not a vocabulary candidate

        const std::string surface = lower(tok);
        if (!hasVowel(surface)) continue;

        const std::string lemma = lemmatize(surface);
        if (!inTable(lemma)) continue;                  // static word list: absent means drop
        if (rankOf(lemma) <= minFreqRank) continue;     // within the level's range: mastered
        if (knownLemmas.count(lemma) != 0) continue;    // known set
        if (!seen.insert(lemma).second) continue;       // same lemma once per excerpt

        spdlog::trace("filterWords: candidate surface='{}' lemma='{}'", surface, lemma);
        out.push_back({surface, lemma});
    }

    spdlog::trace("filterWords: {} chars -> {} candidate(s), minFreqRank={}", size, out.size(),
                  minFreqRank);
    return out;
}

}
