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
├── data			 # wordlist
├── third_party
├── i18n              # .qm/ts files
├── icons
├── src
├── test              # 自检：单测（样例集）+ 冒烟，根目录独立
└──  ui-prototypes/
     └── v1-halo-{tray-menu,stats,words,cost,settings,bubble}.html  
```

## Commands

Toolchain (verified): cmake 4.0.1 · MSVC 19.44 (VS 2022) · Qt 6.9.0 MSVC2022_64 @ `B:/qtt/6.9.0/msvc2022_64`; preset `vs-qt6`.

```
cmake --preset vs-qt6
cmake --build --preset vs-qt6 --config Debug
```

- Before running: put `B:/qtt/6.9.0/msvc2022_64/bin` on `PATH` and set `QT_FORCE_STDERR_LOGGING=1`. Without them the exe cannot find `Qt6Core.dll`, and Qt logs are swallowed silently — a clean exit code does not mean it worked.
- Targets: `lens_core` (no Qt) → `lens_llm` → `lens_app`. `lens_test` is standalone and never shipped (`--filter` offline self-check / `--smoke` real LLM) — see `PHASE1.md` §4.5.

## Reference Documents

- `CONTEXT.md` — glossary
- `DESIGN.md` — design decisions
- `UI.md` — UI spec
- `PHASE1.md` — phase 1 implementation contract (scope, module interfaces, prompt/schema)
- `ui-prototypes/v1-halo-*.html` — prototype, one file per surface
- `TODO.md ` — waiting for implement
