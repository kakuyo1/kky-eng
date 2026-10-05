# Changelog

## 1.0.0 - 2026-10-05

First release of Lens, a Windows desktop English-learning tool that explains selected words,
entities, and sentences without opening a main window.

### Added

- Tray-resident interface with selection actions, explanation bubbles, settings, statistics, word
  history, and cost views.
- Word explanations with English, Chinese, IPA, caching, and learned/new-word marking.
- Entity explanations and sentence translation or explanation through separate LLM channels.
- Local selection filtering, redaction, response validation, and stale-response protection.
- Per-user settings storage under `%APPDATA%\Lens`, keeping API credentials out of the install tree
  and application logs.
- Windows installer built with Windeployqt and Inno Setup.

### Changed

- Selection classification now follows the shape of the selected text: a dictionary word, an entity,
  or a sentence.
- The project includes offline unit tests, Qt Quick component and surface tests, integration tests,
  performance checks, and a bounded real-model smoke test.

### Fixed

- Double-click and triple-click no longer open the selection action bar.
- Bubble placement no longer flashes during its first animation frame.
