# CLAUDE.md — Lens Screen English Assistant

## Role & Purpose

You are the AI assistant for Lens, a Windows desktop English-learning tool built in C++ / QML (Qt 6). You help with development, debugging, and code quality. The project is still in the design phase; no source code yet.

## Critical Rules

- **Tray-only, no main window.** Three independent surfaces are the only UI: tray icon + context menu, explanation overlay, settings popup. Never introduce a main window, a settings page, or a persistent panel.
- No commits unless explicitly requested.
- UI work must comply with `UI.md` (Option 1: minimal light + glass overlay) and `DESIGN.md`.
- Keep `CONTEXT.md`, `DESIGN.md`, and `UI.md` in sync on any design change; record major trade-offs as ADRs.
- Locked decisions (see `DESIGN.md`) are non-negotiable: minimize what each channel sends and sanitize before sending; levels and known-set never enter the LLM prompt; the static wordlist is the sole authority on "is it a real word"; the overlay reveals [已会]/[再学] only on hover, auto-dismisses in ~5s, and has a global pause hotkey.
- No AI attribution in commits or PRs. Keep code, comments, and commit messages concise. Default to Chinese in replies.

## Project Structure

```
demo/
├── CONTEXT.md        # Glossary (authoritative terms)
├── DESIGN.md         # Design decisions (channels, pipeline, modules, prototype path)
├── UI.md             # UI spec (Option 1, final)
├── ui-prototypes/
│   └── v1-halo.html  # Option 1 prototype (self-contained single file)
└── CLAUDE.md
```

No source code yet. Planned module split in `DESIGN.md`: FilterCore / KnownStore / LlmClient / Overlay / Demo.

## Commands

Qt 6 + CMake toolchain is not confirmed; build and check commands are TBD. Confirm the toolchain path before starting the prototype.

- View the prototype: open `ui-prototypes/v1-halo.html` in a browser
- Lint Chinese docs (relative path):
  ```bash
  npx -y zhlint --config ~/.claude/skills/zhlint/assets/zhlintrc.json <file.md>
  ```
- Prototype path (no OCR, see `DESIGN.md`): fake "stable snapshot" text → FilterCore → fake LLM response → overlay
- i18n: UI chrome is Chinese; explanation language is a runtime setting (en/zh), so no `.ts` multi-language flow yet

Skills to load: `cpp-oop-style` before writing C++, `api-and-interface-design` before defining module boundaries, `writing-prompt` before writing LLM prompts/schemas, `context7` to verify third-party library APIs.

## Reference Documents

- `CONTEXT.md` — glossary
- `DESIGN.md` — design decisions
- `UI.md` — UI spec
- `ui-prototypes/v1-halo.html` — Option 1 prototype
- The global `~/.claude/CLAUDE.md` also applies (coding discipline, skill loading, Chinese output)
