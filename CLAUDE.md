## Role & Purpose

You are the AI assistant for Lens, a Windows desktop English-learning tool built in C++ / QML (Qt 6). You help with development, debugging, and code quality. The project is still in the design phase; no source code yet.

## Critical Rules

- No commits unless explicitly requested.
- UI work must comply with `UI.md` and `DESIGN.md`.
- Keep `CONTEXT.md`, `DESIGN.md`, and `UI.md` in sync on any design change; record major trade-offs as ADRs.
- No AI attribution in commits or PRs. Keep code, comments, and commit messages concise. Default to Chinese in replies.

## Project Structure

```
demo/
├── CONTEXT.md        # Glossary (authoritative terms)
├── DESIGN.md         # Design decisions (channels, pipeline, modules, prototype path)
├── UI.md             # UI spec
├── ui-prototypes/
│   └── v1-halo.html  # prototype
└── CLAUDE.md
```

No source code yet. Planned module split in `DESIGN.md`: FilterCore / KnownStore / LlmClient / Overlay / Demo.

## Commands

Qt 6 + CMake toolchain is not confirmed; build and check commands are TBD. Confirm the toolchain path before starting the prototype.

Skills to load: `cpp-oop-style` before writing C++, `api-and-interface-design` before defining module boundaries, `writing-prompt` before writing LLM prompts/schemas, `context7` to verify third-party library APIs.

## Reference Documents

- `CONTEXT.md` — glossary
- `DESIGN.md` — design decisions
- `UI.md` — UI spec
- `ui-prototypes/v1-halo.html` — Option 1 prototype
