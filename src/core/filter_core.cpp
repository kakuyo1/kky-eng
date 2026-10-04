#include "core/filter_core.h"
#include "core/log.h"
#include "core/profile.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace lens::core {
namespace {

using Table = std::unordered_map<std::string, std::size_t>;

/// Word to 1-based frequency rank. Empty means the word list has not been loaded.
Table g_rank;

bool isLetter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}
bool isAlnum(char c)
{
    return isLetter(c) || isDigit(c);
}
bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

std::string lower(std::string_view s);

std::string trimPunctuation(std::string_view token)
{
    std::size_t first = 0;
    std::size_t last = token.size();
    while (first < last && !isLetter(token[first]))
        ++first;
    while (last > first && !isLetter(token[last - 1]))
        --last;
    return std::string(token.substr(first, last - first));
}

bool isTitleCaseToken(const std::string& token)
{
    if (token.size() < 2 || !isLetter(token.front()) || token.front() < 'A' || token.front() > 'Z')
        return false;
    for (std::size_t i = 1; i < token.size(); ++i)
        if (token[i] < 'a' || token[i] > 'z')
            return false;
    return true;
}

bool isEntitySelection(std::string_view text)
{
    std::vector<std::string> tokens;
    std::size_t start = 0;
    while (start < text.size()) {
        while (start < text.size() && isSpace(text[start]))
            ++start;
        const std::size_t end = text.find_first_of(" \t\n\r\v\f", start);
        const std::size_t stop = end == std::string_view::npos ? text.size() : end;
        if (start < stop) {
            const std::string token = trimPunctuation(text.substr(start, stop - start));
            if (token.empty())
                return false;
            tokens.push_back(token);
        }
        if (end == std::string_view::npos)
            break;
        start = end;
    }

    if (tokens.size() < 2 || tokens.size() > 5)
        return false;

    std::size_t titleCaseCount = 0;
    for (const std::string& token : tokens) {
        if (isTitleCaseToken(token)) {
            ++titleCaseCount;
            continue;
        }
        const std::string lowered = lower(token);
        if (lowered != "of" && lowered != "the" && lowered != "and" && lowered != "for")
            return false;
    }
    return titleCaseCount >= 2;
}

/// @return The token when the whole (trimmed) selection is one letters-only token of at least
/// two letters, else an empty view. The reader picked exactly this, so it is a word they asked
/// about whatever its case, vowel, or place in the static list; a digit or interior punctuation
/// disqualifies it (MP3, R2D2, and two words all stay out).
std::string_view loneToken(std::string_view text)
{
    std::size_t head = 0;
    std::size_t tail = text.size();
    while (head < tail && isSpace(text[head]))
        ++head;
    while (tail > head && isSpace(text[tail - 1]))
        --tail;
    const std::string_view body = text.substr(head, tail - head);
    std::size_t first = 0;
    std::size_t last = body.size();
    while (first < last && !isAlnum(body[first]))
        ++first;
    while (last > first && !isAlnum(body[last - 1]))
        --last;
    const std::string_view token = body.substr(first, last - first);
    if (token.size() < 2)
        return {};
    for (char c : token)
        if (!isLetter(c))
            return {}; // a digit or interior punctuation disqualified it
    return token;
}

char toLower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out)
        c = toLower(c);
    return out;
}

bool inTable(const std::string& word)
{
    return g_rank.contains(word);
}

/// @return The word's rank, or 0 when the word is absent from the list.
std::size_t rankOf(const std::string& word)
{
    const auto it = g_rank.find(word);
    return it == g_rank.end() ? 0 : it->second;
}

bool endsWith(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// Words that look inflected but are their own base form, and whose "stem" happens to be
/// another word in the list. Intercepted before any suffix rule can fire.
/// Only these two so far; add more when the sample set catches another.
const std::unordered_set<std::string>& keepAsIs()
{
    static const std::unordered_set<std::string> words = {"news", "means"};
    return words;
}

/// Inflected form to its base form(s), loaded from data/irregulars.tsv. A form with more
/// than one base (better -> good / well) carries them all; the empty map means
/// loadIrregulars() has not run.
std::unordered_map<std::string, std::vector<std::string>> g_irregulars;
bool g_irregularsLoaded = false;

/// Every candidate must already be a real word list entry. A stem that is not in the list
/// is meaningless and would only split "water" into "wat".
void pushIfInTable(std::vector<std::string>& out, std::string stem)
{
    if (stem.size() >= 3 && inTable(stem)) out.push_back(std::move(stem));
}

/// Applies the doubled-consonant rule: "running" -> "runn" -> "run". Requires at least
/// 3 letters left over, which blocks the degenerate "inning" -> "in".
void pushDoubledFixed(std::vector<std::string>& out, const std::string& stem)
{
    if (stem.size() < 4 || stem[stem.size() - 1] != stem[stem.size() - 2]) return;
    pushIfInTable(out, stem.substr(0, stem.size() - 1));
}

/// @note 'y' counts as a vowel here, which rescues rhythm / myth / gym.
bool hasVowel(const std::string& s)
{
    for (char c : s)
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y') return true;
    return false;
}

} // namespace

void loadWordlist(const std::filesystem::path& path)
{
    const auto startedAt = std::chrono::steady_clock::now();
    LENS_TRACE("loadWordlist: reading '{}'", path.string());

    std::ifstream in(path);
    if (!in) {
        LENS_CRITICAL("loadWordlist: cannot open word list '{}'", path.string());
        throw std::runtime_error("Cannot open the word list: " + path.string());
    }

    Table table;
    std::string line;
    std::size_t rank = 0;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (line.empty()) continue;
        ++rank;
        table.emplace(lower(line), rank); // duplicates keep their earliest rank
    }
    if (table.empty()) {
        LENS_CRITICAL("loadWordlist: word list '{}' is empty", path.string());
        throw std::runtime_error("The word list is empty: " + path.string());
    }
    g_rank = std::move(table);

    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - startedAt)
                               .count();
    LENS_INFO("wordlist loaded: {} entries from '{}' in {} ms", g_rank.size(), path.string(), elapsedMs);
}

void loadIrregulars(const std::filesystem::path& path)
{
    const auto startedAt = std::chrono::steady_clock::now();
    LENS_TRACE("loadIrregulars: reading '{}'", path.string());

    std::ifstream in(path);
    if (!in) {
        LENS_CRITICAL("loadIrregulars: cannot open '{}'", path.string());
        throw std::runtime_error("Cannot open the irregular table: " + path.string());
    }

    std::unordered_map<std::string, std::vector<std::string>> table;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (line.empty() || line.front() == '#') continue;

        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            LENS_CRITICAL("loadIrregulars: line {} is neither blank, '#' nor 'form<TAB>base'",
                          lineNumber);
            throw std::runtime_error("Irregular table line " + std::to_string(lineNumber) +
                                     " is malformed: " + path.string());
        }

        const std::string form = lower(std::string_view(line).substr(0, tab));
        const std::string base = lower(std::string_view(line).substr(tab + 1));
        if (form.empty() || base.empty() || form == base) {
            LENS_CRITICAL("loadIrregulars: line {} carries no usable pair", lineNumber);
            throw std::runtime_error("Irregular table line " + std::to_string(lineNumber) +
                                     " carries no usable pair: " + path.string());
        }

        std::vector<std::string>& bases = table[form];
        if (std::find(bases.begin(), bases.end(), base) == bases.end()) bases.push_back(base);
    }

    if (table.empty()) {
        LENS_CRITICAL("loadIrregulars: '{}' holds no pairs", path.string());
        throw std::runtime_error("The irregular table is empty: " + path.string());
    }
    g_irregulars = std::move(table);
    g_irregularsLoaded = true;

    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - startedAt)
                               .count();
    LENS_INFO("irregular table loaded: {} form(s) from '{}' in {} ms", g_irregulars.size(), path.string(), elapsedMs);
}

std::string lemmatize(std::string_view token)
{
    if (!g_irregularsLoaded)
        throw std::logic_error("lens::core::lemmatize: loadIrregulars() must run first");
    if (g_rank.empty())
        throw std::logic_error("lens::core::lemmatize: loadWordlist() must run first");

    LENS_PROFILE_SCOPE("lemmatize"); // entered once per token, so watch the timer cost

    const std::string t = lower(token);
    if (keepAsIs().count(t) != 0) return t;

    // A loaded irregular is authoritative and does not enter the frequency comparison
    // below: children outranks child in the word list, so comparing would pick children
    // right back. A form with several bases (better -> good / well) has none that can be
    // told apart locally, so those are settled by the same frequency tie-break.
    if (const auto it = g_irregulars.find(t); it != g_irregulars.end()) {
        const std::vector<std::string>& bases = it->second;
        const std::string* best = &bases.front();
        std::size_t bestRank = std::numeric_limits<std::size_t>::max();
        for (const auto& base : bases) {
            const std::size_t rank = rankOf(base);
            if (rank != 0 && rank < bestRank) {
                bestRank = rank;
                best = &base;
            }
        }
        if (*best != t) LENS_TRACE("lemmatize: '{}' -> '{}' (irregular)", t, *best);
        return *best;
    }

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
        pushIfInTable(cands, t.substr(0, n - 1)); // moves -> move
        pushIfInTable(cands, t.substr(0, n - 2)); // goes -> go
    } else if (endsWith(t, "s") && n > 3 && !endsWith(t, "ss")) {
        pushIfInTable(cands, t.substr(0, n - 1));
    }

    if (endsWith(t, "ly") && n > 5) { // -ily was already tried above, mapping to y
        const std::string stem = t.substr(0, n - 2);
        pushIfInTable(cands, stem + "e"); // likely -> like
        pushIfInTable(cands, stem);       // quickly -> quick
    }

    // ponytail: comparatives -er/-est only fire at 6/7+ letters; at 5 letters the stems of
    // offer / under / other are all false reductions.
    // Cost: short-stem comparatives such as biggest -> big and nicer -> nice go
    // unreduced. The overlay then explains the inflected form, which is not wrong, but the
    // word is not merged with its lemma either.
    if (endsWith(t, "er") && n > 5) {
        const std::string stem = t.substr(0, n - 2);
        pushIfInTable(cands, stem + "e"); // larger -> large
        pushIfInTable(cands, stem);       // faster -> fast
        pushDoubledFixed(cands, stem);    // bigger -> big
    }
    if (endsWith(t, "est") && n > 6) {
        const std::string stem = t.substr(0, n - 3);
        pushIfInTable(cands, stem + "e"); // largest -> large
        pushIfInTable(cands, stem);       // fastest -> fast
        pushDoubledFixed(cands, stem);
    }

    if (cands.empty()) return t; // not in the list; the caller's whitelist stage drops it

    const std::string* best = &cands.front();
    std::size_t bestRank = rankOf(*best);
    for (const auto& c : cands) {
        const std::size_t r = rankOf(c);
        if (r < bestRank) {
            bestRank = r;
            best = &c;
        }
    }

    if (*best != t) LENS_TRACE("lemmatize: '{}' -> '{}' (rank {})", t, *best, bestRank);
    return *best;
}

std::vector<Candidate> filterWords(
    std::string_view text,
    const std::unordered_set<std::string>& knownLemmas,
    std::size_t minFreqRank)
{
    if (!g_irregularsLoaded)
        throw std::logic_error("lens::core::filterWords: loadIrregulars() must run first");
    if (g_rank.empty())
        throw std::logic_error("lens::core::filterWords: loadWordlist() must run first");

    LENS_PROFILE_SCOPE("filterWords");

    std::vector<Candidate> out;
    std::unordered_set<std::string> seen; // de-duplication keyed by lemma

    // A selection that is one letters-only token is a word the reader picked by hand, so it is a
    // candidate whatever the gates say -- the static list does not hold most acronyms or
    // identifiers, and qml has no vowel either. In continuous prose those runs are still handled
    // by the ordinary gates; only a selection that is exactly this token qualifies (see
    // check-eval-corpus.py's lone-token rule and test/eval_corpus.json).
    if (const std::string_view token = loneToken(text); !token.empty()) {
        const std::string surface = lower(token);
        const std::string lemma = lemmatize(surface); // RUNNING -> run; an acronym stands as-is
        const CandidateState state = knownLemmas.count(lemma) != 0                    ? CandidateState::Known
                                     : inTable(lemma) && rankOf(lemma) <= minFreqRank ? CandidateState::Mastered
                                                                                      : CandidateState::New;
        LENS_TRACE("filterWords: lone token '{}' -> surface='{}' lemma='{}' state={}",
                   token,
                   surface,
                   lemma,
                   static_cast<int>(state));
        out.push_back({surface, lemma, state});
        return out;
    }

    const std::size_t size = text.size();
    std::size_t i = 0;
    while (i < size) {
        while (i < size && isSpace(text[i]))
            ++i;
        const std::size_t start = i;
        while (i < size && !isSpace(text[i]))
            ++i;
        if (start == i) continue;

        // One whitespace-delimited run of the original text. Trim bytes that are neither
        // letters nor digits from both ends (punctuation, quotes, full-width CJK symbols);
        // if any non-letter byte survives inside, the run is glued junk (URL, email,
        // filename, version2, abbreviation) and is dropped whole. Digits stay inside the
        // token rather than being trimmed, otherwise version2 would be trimmed to version
        // and popped as a real word.
        std::size_t head = start;
        std::size_t tail = i;
        while (head < tail && !isAlnum(text[head]))
            ++head;
        while (tail > head && !isAlnum(text[tail - 1]))
            --tail;
        if (head == tail) continue;

        LENS_PROFILE_COUNT("filterWords/tokens", 1); // every run that survived trimming

        const std::string_view tok = text.substr(head, tail - head);
        bool glued = false;
        for (char c : tok)
            if (!isLetter(c)) {
                glued = true;
                break;
            }
        if (glued) continue;
        if (tok.size() < 3) continue;

        bool allCaps = true;
        for (char c : tok)
            if (c >= 'a' && c <= 'z') {
                allCaps = false;
                break;
            }
        if (allCaps) continue; // THE / NASA / OK: not a vocabulary candidate

        const std::string surface = lower(tok);
        if (!hasVowel(surface)) continue;

        const std::string lemma = lemmatize(surface);
        if (!inTable(lemma)) continue;            // static word list: absent means drop
        if (!seen.insert(lemma).second) continue; // same lemma once per excerpt

        // What the reader has already said about the word annotates it; it does not take the
        // candidate away (TODO.md item 0). An explicit selection is a request to explain the
        // word whatever they once marked, and phase 2's automatic scanning is the caller that
        // will skip these states. Known outranks Mastered: the reader's own mark is the more
        // specific statement of the two.
        const CandidateState state = knownLemmas.count(lemma) != 0  ? CandidateState::Known
                                     : rankOf(lemma) <= minFreqRank ? CandidateState::Mastered
                                                                    : CandidateState::New;

        LENS_TRACE("filterWords: candidate surface='{}' lemma='{}' state={}",
                   surface,
                   lemma,
                   static_cast<int>(state));
        out.push_back({surface, lemma, state});
    }

    LENS_TRACE("filterWords: {} chars -> {} candidate(s), minFreqRank={}", size, out.size(), minFreqRank);
    return out;
}

Selection classifySelection(
    std::string_view text,
    const std::unordered_set<std::string>& knownLemmas,
    std::size_t minFreqRank)
{
    // Entity recognition runs before the word fallback. It is intentionally narrow so a single
    // capitalized token remains a word or sentence instead of being promoted by typography alone.
    Selection selection;
    selection.candidates = filterWords(text, knownLemmas, minFreqRank);
    if (isEntitySelection(text)) {
        selection.kind = SelectionKind::Entity;
        selection.candidates.clear();
    } else {
        selection.kind = selection.candidates.empty() ? SelectionKind::Sentence : SelectionKind::Word;
    }

    LENS_TRACE("classifySelection: {} char(s) -> {}",
               text.size(),
               selection.kind == SelectionKind::Word ? "word" : selection.kind == SelectionKind::Entity ? "entity"
                                                                                                        : "sentence");
    return selection;
}

}
