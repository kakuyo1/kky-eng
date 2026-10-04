## Role & Purpose

You are the AI assistant for Lens, a Windows desktop English-learning tool built in C++ / QML (Qt 6). Phase 1 is the word-explanation channel, no OCR: the pipeline is `PHASE1.md` §3, its scope and the OCR-dependent placeholders are `PHASE1.md` §2.

## Critical Rules

- No commits unless explicitly requested, no AI attribution in commits or PRs.
- UI work must comply with `UI.md` and `PRODUCT.md`; implementation contract in `PHASE1.md`.
- Designing or auditing a UI surface starts by loading the `taste-skill` skill; a surface that has been rendered is checked with `visual-qa`.
- Keep `GLOSSARY.md`, `PRODUCT.md`, `UI.md`, and `PHASE1.md` in sync on any design change; record major trade-offs in `docs/adr/`.
- Default to Chinese in replies.
- The API key lives in `%APPDATA%\Lens\settings.json`, the reader's own profile and never the install directory; it is never logged, never echoed in errors, never committed. The repository's gitignored `settings.local.json` is that document's development source, copied over once on a first run (`src/app/main.cpp`, `PHASE1.md` § 6).

## Project Structure

```
lens/
├── .claude           # project settings: the Qt skill family enabled (settings.json)
├── .clang-format     # code format spec
├── .githooks         # pre-commit: typography budget, clang-format, PROBE, QML, absolute paths
├── cmake             # headers only: the precompiled ones CMakeLists.txt applies
├── config            # paths.json — the machine paths; README.md owns the rule and its exemptions
├── data              # wordlist + llm/ (wire protocol as data)
├── docs              # QML.md (how QML works here) + adr/ (major trade-offs)
├── i18n              # .ts translations; English is the source language
├── installer         # lens.iss — the Inno Setup script; version.iss.in, filled from project()
├── scripts           # build.bat — Ninja + MSVC wrapper; *.ps1 — driving, photographing and timing the real surfaces (TEST.md § 5); qml-lint.sh — the QML gate
├── third_party       # vendored: nlohmann/json, spdlog, googletest
├── icons
├── logs              # runtime logs, rotating, gitignored but for .gitkeep
├── src
├── test              # gtest (unit / integration / perf / smoke) and qtest targets, separate from src/
└── ui-prototypes/
     └── v1-halo-{tray-menu,stats,words,cost,settings,bubble,selection-bar}.html
```

## Build

Toolchain (verified): cmake 4.0.1 · Ninja 1.12.1 · MSVC 19.44

```
./scripts/build.bat                      # configure once, then incremental, into build-ninja
./scripts/build.bat --target lens_gtest_unit     # a test target, when you want one
./scripts/build-release.bat              # the same, into build-ninja-release (RelWithDebInfo)
```

The default build is the application alone. Every test target carries `EXCLUDE_FROM_ALL` in `test/`,
so `all` does not pay for suites that change far less often than `src/` does, and each one is built
by naming it. CI names its own list, the four it runs plus `lens_gtest_perf`, so that gate is
unaffected. `TEST.md` section 2 says when each suite is worth running.

`build-ninja` stays Debug, which is where the assertions, the TRACE log and the PDB
`scripts/coverage.sh` reads come from. `build-ninja-release` is the tree to run the application
from, and it exists as a second tree rather than a build-type switch because switching rewrites
every compile flag and rebuilds the lot. Both are driven by the same wrapper; the two environment
variables `scripts/build-release.bat` sets are the whole difference.

The Qt prefix lives in `config/paths.json`, which `build.bat` exports as `QT_ROOT` before it drives
the presets. `config/README.md` owns the rule: machine paths come from that file, project paths stay
relative to the repository root, and nothing else writes either kind by hand.

## Test

Tests live under `test/googletest/` (gtest): `unit/` offline, `integration/` real Windows APIs (human-run), `perf/` measurement, `smoke/` real model, with `e2e/` the name for what comes next. The Qt and QML side is `test/qtest/` (QTest), which needs a window; it is not here.

```
PATH=/b/qtt/6.9.0/msvc2022_64/bin:$PATH QT_FORCE_STDERR_LOGGING=1 \
  ./build-ninja/test/googletest/lens_gtest_unit.exe
```

`TEST.md` owns the rest: the targets, the corpus format, the profiling facility and its build tree, the coverage scan, and the run-record convention.

Targets: `lens_core` (no Qt) → `lens_llm` → `lens_app`. The `lens_gtest_*` targets are standalone and never shipped — see `PHASE1.md` §4.5.

## Translations

English is the source language: `i18n/lens_en_US.ts` mirrors the source strings, `i18n/lens_zh_CN.ts` carries the Chinese. After adding or changing a reader-facing string:

```
PATH=/b/qtt/6.9.0/msvc2022_64/bin:$PATH lupdate src -ts i18n/lens_en_US.ts i18n/lens_zh_CN.ts
PATH=/b/qtt/6.9.0/msvc2022_64/bin:$PATH lrelease i18n/lens_en_US.ts i18n/lens_zh_CN.ts
```

`lupdate` appends new strings as `unfinished` and leaves existing translations alone, so rerunning is safe. Write the Chinese into `lens_zh_CN.ts` and copy the source text into `lens_en_US.ts`; `lrelease` is the check, and the goal is zero unfinished. The `.qm` files are build output and stay gitignored.

Removing a string needs `lupdate -no-obsolete`: plain `lupdate` keeps the vanished entries in the file as `<translation type="vanished">`, so a deleted surface's strings go on sitting there with nothing to read them.

`lupdate` reads literal arguments only, so every `tr()` spells out its context and its string at the call site; routing them through a helper that takes the context as a parameter extracts nothing.

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

Log through the `LENS_TRACE` / `LENS_DEBUG` / `LENS_INFO` / `LENS_WARN` / `LENS_ERROR` / `LENS_CRITICAL` macros in `src/core/log.h`.

## Lint

`.githooks/pre-commit` is the source of truth for the C++ and documentation checks: the tools, their fallback paths and the per-doc typography budgets all live there, and it checks only the files a commit touches, so a hand run is what covers the rest.

QML is the exception. `scripts/qml-lint.sh`, called by the hook and by CI, owns its own argument list and its warning ratchet, because a .qml file only lints correctly beside its whole module.

## Reference Documents

- `GLOSSARY.md`: glossary
- `PRODUCT.md`: design decisions
- `UI.md`: UI spec
- `PHASE1.md`: phase 1 implementation contract (scope, module interfaces, prompt/schema)
- `TEST.md`: framework, targets, corpus, profiling, run records
- `API.md`: wire format, request body, response envelope, validation rules, error codes
- `CODING_STANDARDS.md`: the judgement calls a review can make (C++ and QML)
- `docs/QML.md`: QML facts, module layout, surfaces vs components, shadows, positioning, threading, fonts
- `docs/adr/`: major trade-offs, one numbered file each; `README.md` has the numbering and template
- `ui-prototypes/v1-halo-*.html`: prototype, one file per surface
- `TODO.md`: waiting for implement
