# Lens

**Stay with the page you are reading.** Lens is a Windows tray companion that explains English words, names, and sentences in small overlays, without opening a separate dictionary window.

[English](README.md) · [简体中文](README.zh-CN.md) · [日本語](README.ja.md) · [Español](README.es.md)

## Made for reading

Select text for a quick explanation, mark a word as known or new, and keep reading. When text cannot be selected, capture a screen region for OCR. Optional automatic scanning is limited to the applications you allow.

Lens sorts captured text on your PC into word, entity, or sentence requests. A word request sends the word, an entity request sends its name, and a sentence request sends the selected sentence. Lens masks URLs, email addresses, and long digit strings before sending. The selected AI provider receives the request and returns the explanation; Lens does not include a local definition dictionary.

Use your own API key with a supported provider or a custom HTTPS endpoint. Choose the explanation language, review cached word explanations, keep known/new marks, and see your word history and model costs. A daily budget can pause requests when it is reached.

## Product principles

- **Keep the reading flow.** Lens lives in the tray and puts explanations beside the text that prompted them.
- **Send only what the request needs.** Candidate filtering and text classification happen locally; the chosen provider handles the explanation.
- **Let readers set the boundaries.** Choose how text is captured, which apps automatic scanning may inspect, which provider to use, and how much to spend in a day.
- **Keep learning history on the reader's PC.** Settings, API key, cached explanations, word marks, and history are stored in the Windows user profile.

## UI concepts

These images are screenshots of the HTML files in [`ui-prototypes/`](ui-prototypes/). They show design concepts, not screenshots of the running application; implementation details can differ.

| Explanation bubble | Selection actions |
| --- | --- |
| ![Lens explanation bubble concept showing a word, pronunciation, definition, and learning status](docs/readme-images/bubble-ui.png) | ![Lens selection action bar concept with translate, explain, and copy actions](docs/readme-images/selection-bar-ui.png) |

| Settings | Word history |
| --- | --- |
| ![Lens settings concept showing its categories](docs/readme-images/settings-ui.png) | ![Lens word history concept with known and new word marks](docs/readme-images/words-ui.png) |

## Languages and appearance

The interface and generated explanations support English, Chinese, Spanish, and Japanese. The interface offers Light, Dark, Forest, and Custom themes, plus an animation switch that respects Windows accessibility settings.

## Privacy and security

Your API key and learning data are stored in `%APPDATA%\Lens\settings.json`. Lens sends selected text to the provider you configure, so avoid sending confidential or sensitive material. Redaction is limited and does not identify every kind of personal information. See [SECURITY.md](SECURITY.md) for data flows, current safeguards, known limits, and how to report a security issue.
