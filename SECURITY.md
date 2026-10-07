# Security

Lens processes text from the Windows desktop and can send selected content to an AI provider chosen by the reader. This page describes the current data flow, safeguards, and known limits. It is not a guarantee that local data or third-party services are risk-free.

## Reporting a vulnerability

Please do not post exploitable details in a public issue. Check whether GitHub's private vulnerability reporting is available for this repository and use it when enabled. If it is unavailable, open a public issue asking for a private reporting channel, without including exploit details or sensitive data.

There is no published response-time or coordinated-disclosure SLA. Please include the affected version or commit, the impact, and steps to reproduce. Avoid sending real API keys, personal data, or private screen contents in a report. We will coordinate disclosure with the reporter when possible.

## Supported releases

The repository has no published release support window or response-time commitment. Do not assume that an older release will receive security fixes.

## Data and trust boundaries

- **Screen and selection data:** Selection capture, screenshot OCR, and optional automatic scanning can read text visible in other Windows applications. Automatic scanning can be limited to an application allowlist. Manual selection and screenshot capture are initiated by the reader.
- **OCR:** Screenshot regions are passed to the bundled or reader-configured Tesseract process on the same PC. OCR text is then filtered locally.
- **AI providers:** Lens sends the content needed for the selected request over HTTPS to the configured provider. Word requests contain the word, entity requests contain the entity name, and sentence requests contain the selected sentence. The provider's retention, training, and access practices are governed by that provider, not by Lens.
- **Local profile:** Settings, the API key, cached explanations, word marks, and learning history are stored in `%APPDATA%\Lens\settings.json`. This file is ordinary local JSON; Lens does not encrypt it with an application-managed key. Uninstalling Lens does not remove it.
- **Logs:** The API key is not intentionally written to logs, but debug and trace logs can contain queried words and other operational details. Logs rotate and are written relative to the application's working directory, which may be inside the installation directory for packaged launches.
- **Clipboard:** Selection capture temporarily reads and changes the Windows clipboard, then attempts to restore its previous contents. Concurrent clipboard changes by another application can prevent a perfect restoration.

## Safeguards in the current code

- Lens classifies and filters captured text locally, and sends only the content required for that request. It does not send a full-screen image to the explanation API.
- Before sending, Lens masks URLs, email addresses, and digit strings of six or more digits. This is limited pattern masking, not complete personal-data detection. Names, addresses, source code, short identifiers, and other sensitive content may remain.
- The API key is stored in the per-user settings file rather than the installation directory. Request and settings logs avoid printing the key itself.
- Model-list requests use HTTPS, a timeout, and response-size and item-count limits. Model responses are parsed and checked against the expected response shape before use.
- OCR input, output, runtime, and child-process lifetime are bounded. OCR runs locally; the resulting text can still be sent to the selected provider if it passes local filtering and the reader's capture settings allow it.

## Reader guidance

- Do not send passwords, private messages, confidential work, personal records, or other material you are not comfortable sharing with your configured provider.
- Review that provider's privacy and retention terms before entering an API key. A custom HTTPS endpoint receives both the key and request content, so configure only an endpoint you trust.
- Leave automatic scanning off unless you need it. When enabled, keep its application allowlist narrow.
- Protect access to your Windows account and user profile. Anyone or any software running as your user may be able to read the settings file and logs.
- To remove local Lens data, exit the application and delete `%APPDATA%\Lens`. This also removes the saved API key, settings, cache, and learning history.

## Scope

This policy covers the Lens application and repository. It does not make claims about the security of Windows, the selected AI provider, custom endpoints, or other software running on the same PC. Security support timelines and response SLAs have not been published.
