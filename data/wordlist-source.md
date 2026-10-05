# Wordlist Source

`wordlist.txt` is a normalized, one-word-per-line coverage list used by the local selection
classifier. It is not a dictionary and carries no definitions.

The expansion applied in phase 2 was based on the ESDB `en_US-large.txt` plain wordlist:

- Source: <https://github.com/en-wl/wordlist-diff/blob/rel-2026.02.25/en_US-large.txt>
- Source index: <https://wordlist.aspell.net/dicts/>
- License information: <https://github.com/en-wl/wordlist/blob/v2/Copyright>

Normalization keeps only the entries ESDB spells in all lowercase ASCII letters, drops duplicates,
and preserves the existing Lens list as the prefix. Entries ESDB capitalizes — proper nouns and
acronyms such as `GitHub`, `SQL`, and `macOS` — are deliberately excluded so the classifier keeps
sending them to the entity channel. The resulting file contains 168,205 entries.

The list is used for coverage in this phase. Exam-level membership and a stable frequency-ranking
policy remain separate future work; this expansion does not claim that appended entries have a
meaningful rank for CET or CEFR thresholds.
