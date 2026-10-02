## Role & Purpose

You are the AI assistant for Lens, a Windows desktop English-learning tool built in C++ / QML (Qt 6). Phase 1 (word-explanation channel, no OCR): clipboard trigger → local filter → DeepSeek (BYOK) → overlay bubble. Auto-scan / hover / frame-select are UI placeholders (need OCR, phase 2).

## Critical Rules

- No commits unless explicitly requested, no AI attribution in commits or PRs.
- UI work must comply with `UI.md` and `DESIGN.md`; implementation contract in `PHASE1.md`.
- Keep `CONTEXT.md`, `DESIGN.md`, `UI.md`, and `PHASE1.md` in sync on any design change; record major trade-offs as ADRs.
- Default to Chinese in replies.
- API key lives only in gitignored `settings.local.json` — never commit it, never log it, never echo it in errors.

## Project Structure

```
lens/
├── .clang-format     # code format spec
├── data              # wordlist + llm/ (wire protocol as data)
├── i18n              # .ts translations; English is the source language
├── scripts           # build.bat — Ninja + MSVC wrapper
├── third_party       # vendored: nlohmann/json, spdlog
├── icons
├── logs              # runtime logs, rotating, gitignored but for .gitkeep
├── src
├── test              # self-check: offline corpus + real-model smoke, separate from src/
└── ui-prototypes/
     └── v1-halo-{tray-menu,stats,words,cost,settings,bubble}.html
```

## Build

Toolchain (verified): cmake 4.0.1 · Ninja 1.12.1 · MSVC 19.44

```
./scripts/build.bat                      # configure once, then incremental
./scripts/build.bat --target lens_test
```

## Test

```
PATH=/b/qtt/6.9.0/msvc2022_64/bin:$PATH QT_FORCE_STDERR_LOGGING=1 \
  ./build-ninja/test/lens_test.exe --filter
```

- `--filter` — offline self-check: sample corpus, KnownStore round trip, LlmClient pure helpers. No network, no API key, CI-safe. Change behaviour by editing `test/eval_corpus.json` first; touch `src/` only once `--filter` goes red.
- `--smoke [word]` — one word through the real model, default `ubiquitous`. Needs a key and spends money, and a human reads the verdict, so it never runs in CI.
- `LENS_LOG_LEVEL=trace` raises verbosity. `--filter` defaults to `info`, `--smoke` to `trace`.

Targets: `lens_core` (no Qt) → `lens_llm` → `lens_app`. `lens_test` is standalone and never shipped — see `PHASE1.md` §4.5.

## Translations

English is the source language, so `i18n/lens_en_US.ts` mirrors the source strings and `i18n/lens_zh_CN.ts` carries the Chinese.

```
PATH=/b/qtt/6.9.0/msvc2022_64/bin:$PATH lupdate src -ts i18n/lens_en_US.ts i18n/lens_zh_CN.ts
PATH=/b/qtt/6.9.0/msvc2022_64/bin:$PATH lrelease i18n/lens_en_US.ts i18n/lens_zh_CN.ts
```

After adding or changing a reader-facing string, run `lupdate`: it appends the new ones as `unfinished` and leaves existing translations alone, so it is safe to rerun. Then write the Chinese into `lens_zh_CN.ts` and copy the source text into `lens_en_US.ts`. `lrelease` is the check — it reports how many entries are unfinished, and the goal is zero. The `.qm` files it writes are build output and stay gitignored.

`lupdate` only reads literal arguments, so every `tr()` / `translate()` call spells out its context and its string at the call site; routing them through a helper that takes the context as a parameter would extract nothing. Scan `src` today; add the QML directory to the `lupdate` command once the app surfaces exist.

## Code Style

Comments are English and Doxygen-style. `///` with `@brief`, `@param`, `@return`, `@throws` on declarations; a `/** @file ... */` block at the top of each file.

```cpp
/**
 * @brief Reduce a token to its dictionary form.
 * @param token Word to reduce, case-insensitive.
 * @return The lemma; the known set and the cache are keyed by lemma, not by the form written.
 * @throws std::logic_error If loadWordlist() has not run.
 */
std::string lemmatize(std::string_view token);
```

Log through the `LENS_TRACE` / `LENS_DEBUG` / `LENS_INFO` / `LENS_WARN` / `LENS_ERROR` / `LENS_CRITICAL` macros in `src/core/log.h`

## Reference Documents

- `CONTEXT.md` — glossary
- `DESIGN.md` — design decisions
- `UI.md` — UI spec
- `PHASE1.md` — phase 1 implementation contract (scope, module interfaces, prompt/schema)
- `LLM.md` — wire format: request body, response envelope, validation rules, error codes
- `ui-prototypes/v1-halo-*.html` — prototype, one file per surface
- `TODO.md` — waiting for implement
