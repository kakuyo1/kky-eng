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
├── data              # wordlist
├── scripts           # build.bat — Ninja + MSVC wrapper
├── third_party       # vendored: nlohmann/json, spdlog
├── i18n              # .qm/ts files
├── icons
├── src
├── test              # self-check: offline corpus + real-model smoke, separate from src/
└── ui-prototypes/
     └── v1-halo-{tray-menu,stats,words,cost,settings,bubble}.html
```

## Build

Toolchain (verified): cmake 4.0.1 · Ninja 1.12.1 · MSVC 19.44 (VS 2022 @ `D:\VS 2022`) · Qt 6.9.0 MSVC2022_64 @ `B:/qtt/6.9.0/msvc2022_64`.

```
./scripts/build.bat                      # configure once, then incremental
./scripts/build.bat --target lens_test
```

Ninja cannot find MSVC on its own, so `scripts/build.bat` enters the Visual Studio environment (`vswhere` → `vcvars64.bat`) before driving CMake. Running `cmake --preset ninja-qt6` then `cmake --build --preset ninja-qt6` by hand works from a VS developer command prompt. `preset vs-qt6` (Visual Studio generator, `build/`) stays for IDE work.

The build preset caps Ninja at 4 parallel jobs. At 16 jobs the concurrent `cl.exe` processes exhaust the machine's memory and every translation unit dies with `C1060: compiler is out of heap space`; raise the cap via `CMAKE_BUILD_PARALLEL_LEVEL` only when the machine has headroom. A changed source file rebuilds in ~6 s.

## Test

Put the Qt bin directory on `PATH` first or the exe cannot find `Qt6Core.dll`, and set `QT_FORCE_STDERR_LOGGING=1` or Qt swallows its own logs — a clean exit code does not mean it worked.

```
PATH=/b/qtt/6.9.0/msvc2022_64/bin:$PATH QT_FORCE_STDERR_LOGGING=1 \
  ./build-ninja/test/lens_test.exe --filter
```

- `--filter` — offline self-check: sample corpus, KnownStore round trip, LlmClient pure helpers. No network, no API key, CI-safe. Change behaviour by editing `test/eval_corpus.json` first; touch `src/` only once `--filter` goes red.
- `--smoke [word]` — one word through the real model, default `ubiquitous`. Needs a key and spends money, and a human reads the verdict, so it never runs in CI.
- `LENS_LOG_LEVEL=trace` raises verbosity. `--filter` defaults to `info`, `--smoke` to `trace`.

Targets: `lens_core` (no Qt) → `lens_llm` → `lens_app`. `lens_test` is standalone and never shipped — see `PHASE1.md` §4.5.

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

Chinese stays where a human or the model reads it: the model prompt, user-facing messages (exception texts, `lens_test` diagnostics), and the `*.md` design docs.

Log through spdlog — `spdlog::trace` for per-call pipeline detail, `info` for lifecycle milestones, `error` or `critical` for failures. Every module logs; `src/core/log.h` installs the logger.

## Reference Documents

- `CONTEXT.md` — glossary
- `DESIGN.md` — design decisions
- `UI.md` — UI spec
- `PHASE1.md` — phase 1 implementation contract (scope, module interfaces, prompt/schema)
- `ui-prototypes/v1-halo-*.html` — prototype, one file per surface
- `TODO.md` — waiting for implement
