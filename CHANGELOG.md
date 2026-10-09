# Changelog

## 1.1.0 - 2026-10-09

Second release of Lens. Capture reaches past the selection to the screen itself, an explanation
reaches past one sense, and the tool gains its own themes, model selection and installer.

### Added

- Screen capture from a global hotkey, `Ctrl+Alt+S` by default: every screen is dimmed, the framed
  region is read with OCR, and its translation appears without a second question from the action bar.
- Automatic scanning: text that holds still in a whitelisted window is offered on its own.
- A capture page: switches for OCR and scanning, a two-to-five letter minimum word length, a process
  whitelist, and the OCR engine's own paths for a reader who has an installed copy.
- Several senses per word, up to three, behind the multi-sense switch.
- A word's origin beside its definition, fetched with it rather than in a second request.
- Spanish and Japanese explanations and interface, from the same language table as English and
  Chinese.
- A model service page, with the model field filtering the catalog's list as the reader types.
- A daily budget cap: when the day's spend reaches it, capture pauses rather than failing.
- Removing a word from the word list, and a year of lookups drawn as a contribution graph.
- Hovering a word in the list to read the explanation already stored for it.
- A Forest theme, reader-defined colours checked for contrast, and a switch for interface motion that
  follows the Windows reduced-motion preference.
- An installer whose pages carry the product's own colours and fonts, with the OCR runtime bundled.

### Changed

- The word list is a 168k common-English list rather than a word-frequency table.
- Providers, models, prices and prompts all come from `data/llm/`, so a new provider or language is a
  data change.
- The model list comes from one shared catalog request, asked for once per launch and sent without the
  reader's key or anything about what they are reading; the answer never overwrites the model in
  force.
- Helpers the modules had each grown their own copy of, and the logging code, now live in `src/util`,
  covered by offline tests.

### Fixed

- A long sentence no longer overflows the notice and leaves it unclosable.
- A connection that fails after an HTTP 200 is reported as a transport failure, not a schema error.

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
