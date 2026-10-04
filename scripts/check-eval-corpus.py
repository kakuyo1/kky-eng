#!/usr/bin/env python3
"""Check test/eval_corpus.json against the documented FilterCore rules.

The corpus is FilterCore's behaviour spec (PHASE1.md section 4.5): a behaviour change is
written into the corpus first, and the suite stays red until src/ catches up. That makes the
corpus itself the thing everything rests on -- and a wrong expectation in it is invisible to
the suite, because the implementation agrees with it by construction. This is what looks at
the corpus on its own.

It restates the rules from PHASE1.md section 4.1 and the header comment of
src/core/filter_core.h rather than calling the implementation, so a disagreement is a finding
to read, not a check that failed. Every token of an excerpt must be accounted for: one that
survives the hard filters and the static word list belongs in `expect`, and one that does not
is fine as long as a named rule excludes it.

    python scripts/check-eval-corpus.py [test/eval_corpus.json]

`expectLemmas` is not verified: the reduction rules produce it, and restating them here would
be restating the implementation rather than a rule a reader can check.
"""

from __future__ import annotations

import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

VOWELS = set("aeiouy")

IRREGULARS: dict[str, list[str]] = {}


def load_irregulars() -> None:
    """Fill form -> base forms, as loadIrregulars() builds it from data/irregulars.tsv."""
    with open(os.path.join(ROOT, "data", "irregulars.tsv"), encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            form, _, base = line.partition("\t")
            IRREGULARS.setdefault(form.lower(), []).append(base.lower())


def load_wordlist() -> dict[str, int]:
    """Return word -> 1-based frequency rank, as loadWordlist() builds it."""
    ranks: dict[str, int] = {}
    with open(os.path.join(ROOT, "data", "wordlist.txt"), encoding="utf-8") as handle:
        for rank, line in enumerate(handle, start=1):
            word = line.strip().lower()
            if word and word not in ranks:  # duplicates keep their earliest rank
                ranks[word] = rank
    return ranks


def is_alnum(ch: str) -> bool:
    return ch.isascii() and ch.isalnum()


def tokens(text: str) -> list[str]:
    """The whitespace-delimited runs of `text` that survive trimming (filterWords' tokenise)."""
    out = []
    for run in text.split():
        head, tail = 0, len(run)
        while head < tail and not is_alnum(run[head]):
            head += 1
        while tail > head and not is_alnum(run[tail - 1]):
            tail -= 1
        if head != tail:
            out.append(run[head:tail])
    return out


def reject_reason(token: str, ranks: dict[str, int]) -> str | None:
    """@return Why the token is not a candidate, or None when it is one."""
    if any(not ch.isalpha() or not ch.isascii() for ch in token):
        return "glued: a non-letter inside the token"
    if len(token) < 3:
        return "short: under 3 letters"
    if not any(ch.islower() for ch in token):
        return "all caps"
    lowered = token.lower()
    if not VOWELS.intersection(lowered):
        return "no vowel"
    if not any(stem in ranks for stem in reductions(lowered)):
        return "not in the static word list"
    return None


def reductions(word: str) -> list[str]:
    """The word itself, its irregular bases, and the documented suffix stems.

    Only the question "is any of these a word list entry" is asked here, which is why this
    can stop short of lemmatize(): the rank comparison that picks between them does not
    change whether the token is admitted.
    """
    stems = [word, *IRREGULARS.get(word, ())]
    n = len(word)

    def push(stem: str) -> None:
        if len(stem) >= 3:
            stems.append(stem)

    if word.endswith("ies") and n > 4:
        push(word[:-3] + "y")
    if word.endswith("ied") and n > 4:
        push(word[:-3] + "y")
    if word.endswith("ier") and n > 5:
        push(word[:-3] + "y")
    if word.endswith("iest") and n > 6:
        push(word[:-4] + "y")
    if word.endswith("ily") and n > 5:
        push(word[:-3] + "y")

    def verb(cut: int) -> None:
        if n <= 4:
            return
        stem = word[:-cut]
        push(stem + "e")
        push(stem)
        if len(stem) >= 4 and stem[-1] == stem[-2]:
            push(stem[:-1])

    if word.endswith("ing"):
        verb(3)
    if word.endswith("ed"):
        verb(2)

    if word.endswith("es") and n > 3:
        push(word[:-1])
        push(word[:-2])
    elif word.endswith("s") and n > 3 and not word.endswith("ss"):
        push(word[:-1])

    if word.endswith("ly") and n > 5:
        push(word[:-2] + "e")
        push(word[:-2])
    if word.endswith("er") and n > 5:
        push(word[:-2] + "e")
        push(word[:-2])
        if word[-3] == word[-4]:
            push(word[:-3])
    if word.endswith("est") and n > 6:
        push(word[:-3] + "e")
        push(word[:-3])
        if word[-4] == word[-5]:
            push(word[:-4])
    return stems


def check_entry(index: int, entry: dict, ranks: dict[str, int]) -> list[str]:
    """@return One message per thing this entry leaves unaccounted for."""
    where = f"entry {index}"
    problems: list[str] = []

    for field in ("text", "expect", "expectKind"):
        if field not in entry:
            return [f"{where}: no '{field}'"]

    text = entry["text"]
    expect = entry["expect"]
    kind = entry["expectKind"]
    known = set(entry.get("known", []))

    if kind not in ("Word", "Sentence"):
        problems.append(f"{where}: expectKind '{kind}' is neither Word nor Sentence")
    wanted = "Word" if expect else "Sentence"
    if kind != wanted:
        problems.append(f"{where}: expectKind is '{kind}' but expect is {'empty' if not expect else 'not empty'}")

    for word in expect:
        if word != word.lower():
            problems.append(f"{where}: expect carries '{word}', not lower-cased")

    if len(set(expect)) != len(expect):
        problems.append(f"{where}: the same surface appears twice in expect")

    # Order: by first appearance in the text.
    order = []
    for token in tokens(text):
        if token.lower() in expect and token.lower() not in order:
            order.append(token.lower())
    if order != expect:
        problems.append(f"{where}: expect is {expect}, first appearance gives {order}")

    # Every token either survives into expect or is excluded by a named rule. A survivor that
    # is absent is the interesting case: it is either a missed expectation or a lemma the
    # excerpt already carried (de-duplication), which is why the two are called apart.
    for token in tokens(text):
        lowered = token.lower()
        reason = reject_reason(token, ranks)
        if lowered in expect:
            if reason is not None:
                problems.append(f"{where}: expect carries '{lowered}', which {reason}")
        elif reason is None and not any(lowered in reductions(kept) for kept in expect):
            # Absent and admitted is only fine when an earlier candidate already carries the
            # same lemma: de-duplication keeps the first surface and drops this one.
            problems.append(f"{where}: '{lowered}' survives the rules but is not in expect")

    # The known set and the frequency band annotate a candidate now, they do not filter it
    # (TODO.md item 0), so neither can have taken a word out of expect.
    surfaces = [token.lower() for token in tokens(text)]
    for lemma in known:
        if lemma.lower() in surfaces and lemma.lower() not in expect:
            problems.append(f"{where}: known lemma '{lemma}' is in the excerpt but not in expect")

    return problems


def main() -> None:
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "test", "eval_corpus.json")
    ranks = load_wordlist()
    load_irregulars()
    with open(path, encoding="utf-8") as handle:
        corpus = json.load(handle)

    problems: list[str] = []
    for index, entry in enumerate(corpus, start=1):
        problems.extend(check_entry(index, entry, ranks))

    for message in problems:
        print(message)
    print(f"corpus: {len(corpus)} entries, {len(problems)} problem(s)")
    raise SystemExit(1 if problems else 0)


if __name__ == "__main__":
    main()
