`本文档仅收录用户实际体验时发生的 BUG`

## 1.中文语言下，翻译一段长句时，http 返回失败，且弹窗文字溢出，并且弹窗无法关闭，看不到关闭按钮，导致用户必须重启
```t
[2026-10-05 18:18:19.474] [info] [t:17320] [app_controller.cpp:235] selection of 11 character(s): sentence channel
[2026-10-05 18:18:20.205] [info] [t:17320] [known_store.cpp:185] settings saved: level=2 lang=en marked=21 cached=18
[2026-10-05 18:18:20.222] [info] [t:17320] [llm_client.cpp:76] explaining 1 item(s) on channel 'sentence' preset 'translate' with 'deepseek-flash'
[2026-10-05 18:18:20.938] [info] [t:17320] [llm_client.cpp:102] received 1 explanation(s)
[2026-10-05 18:18:20.940] [info] [t:17320] [known_store.cpp:185] settings saved: level=2 lang=en marked=21 cached=18
[2026-10-05 18:18:20.940] [info] [t:17320] [app_controller.cpp:367] bubble up for 'post-coital' on 'sentence' channel
[2026-10-05 18:20:22.130] [info] [t:17320] [selection_text_grabber.cpp:357] SelectionTextGrabber::grab: 'C:\Program Files\WindowsApps\Microsoft.WindowsTerminal_1.24.12741.0_x64__8wekyb3d8bbwe\WindowsTerminal.exe' is excluded; Ctrl+C there is an interrupt
[2026-10-05 18:21:33.670] [info] [t:17320] [selection_text_grabber.cpp:357] SelectionTextGrabber::grab: 'C:\Program Files\WindowsApps\Microsoft.WindowsTerminal_1.24.12741.0_x64__8wekyb3d8bbwe\WindowsTerminal.exe' is excluded; Ctrl+C there is an interrupt
[2026-10-05 18:22:25.085] [warning] [t:17320] [:] qt: Retrying to obtain clipboard.
[2026-10-05 18:22:25.153] [warning] [t:17320] [selection_text_grabber.cpp:410] SelectionTextGrabber::grab: the clipboard changed but carried no text
[2026-10-05 18:22:25.153] [warning] [t:17320] [:] qt: Retrying to obtain clipboard.
[2026-10-05 18:22:25.212] [warning] [t:17320] [:] qt: Retrying to obtain clipboard.
[2026-10-05 18:22:27.156] [warning] [t:17320] [:] qt: Retrying to obtain clipboard.
[2026-10-05 18:22:27.231] [warning] [t:17320] [selection_text_grabber.cpp:410] SelectionTextGrabber::grab: the clipboard changed but carried no text
[2026-10-05 18:22:28.890] [warning] [t:17320] [:] qt: Retrying to obtain clipboard.
[2026-10-05 18:22:28.949] [info] [t:17320] [selection_text_grabber.cpp:417] SelectionTextGrabber::grab: captured 3 characters from 'C:\Program Files\Google\Chrome\Application\chrome.exe'
[2026-10-05 18:22:28.949] [info] [t:17320] [app_controller.cpp:253] selection of 3 character(s): word channel, 'the' -> 'the'
[2026-10-05 18:22:30.771] [info] [t:17320] [selection_text_grabber.cpp:417] SelectionTextGrabber::grab: captured 162 characters from 'C:\Program Files\Google\Chrome\Application\chrome.exe'
[2026-10-05 18:22:30.771] [info] [t:17320] [app_controller.cpp:235] selection of 162 character(s): sentence channel
[2026-10-05 18:22:32.428] [warning] [t:17320] [:] qt: Retrying to obtain clipboard.
[2026-10-05 18:22:32.487] [warning] [t:17320] [:] qt: Retrying to obtain clipboard.
[2026-10-05 18:22:32.550] [info] [t:17320] [selection_text_grabber.cpp:417] SelectionTextGrabber::grab: captured 496 characters from 'C:\Program Files\Google\Chrome\Application\chrome.exe'
[2026-10-05 18:22:32.550] [info] [t:17320] [app_controller.cpp:235] selection of 496 character(s): sentence channel
[2026-10-05 18:22:33.378] [info] [t:17320] [known_store.cpp:185] settings saved: level=2 lang=en marked=21 cached=18
[2026-10-05 18:22:33.395] [info] [t:17320] [llm_client.cpp:76] explaining 1 item(s) on channel 'sentence' preset 'translate' with 'deepseek-flash'
[2026-10-05 18:22:34.980] [info] [t:17320] [llm_client.cpp:102] received 1 explanation(s)
[2026-10-05 18:22:34.984] [info] [t:17320] [known_store.cpp:185] settings saved: level=2 lang=en marked=21 cached=18
[2026-10-05 18:22:34.984] [info] [t:17320] [app_controller.cpp:367] bubble up for 'The first command of a session auto-starts the browser daemon, and that
daemon inherits the command's stdout while living up to an hour. A caller
that reads stdout to EOF — the Bash tool, `| cat`, `$(...)`, a captured
`&&` chain — keeps waiting long after the command printed its answer and
exited, so a cold start is indistinguishable from a permanent freeze. It
recurs whenever no daemon is running: first call of a session, and after
`close`, `close --all`, a crash, or the idle timeout.' on 'sentence' channel
[2026-10-05 18:22:42.416] [info] [t:17320] [known_store.cpp:185] settings saved: level=2 lang=en marked=21 cached=18
[2026-10-05 18:22:42.419] [info] [t:17320] [main.cpp:168] interface language is now 'zh'
[2026-10-05 18:22:42.428] [warning] [t:17320] [:] qt: DirectWrite: CreateFontFaceFromHDC() failed (指示输入文件 (例如字体文件) 中的错误。) for QFontDef(Family="MS Sans Serif", pointsize=9.75, pixelsize=13, styleHint=5, weight=700, stretch=100, hintingPreference=0) LOGFONT("MS Sans Serif", lfWidth=0, lfHeight=-13) dpi=96
[2026-10-05 18:22:42.439] [warning] [t:17320] [:] qt: DirectWrite: CreateFontFaceFromHDC() failed (指示输入文件 (例如字体文件) 中的错误。) for QFontDef(Family="MS Sans Serif", pointsize=8.25, pixelsize=13, styleHint=5, weight=400, stretch=100, hintingPreference=0) LOGFONT("MS Sans Serif", lfWidth=0, lfHeight=-13) dpi=96
[2026-10-05 18:22:44.720] [warning] [t:17320] [:] qt: Retrying to obtain clipboard.
[2026-10-05 18:22:44.774] [warning] [t:17320] [:] qt: Retrying to obtain clipboard.
[2026-10-05 18:22:44.836] [info] [t:17320] [selection_text_grabber.cpp:417] SelectionTextGrabber::grab: captured 496 characters from 'C:\Program Files\Google\Chrome\Application\chrome.exe'
[2026-10-05 18:22:44.837] [info] [t:17320] [app_controller.cpp:235] selection of 496 character(s): sentence channel
[2026-10-05 18:22:45.878] [info] [t:17320] [known_store.cpp:185] settings saved: level=2 lang=en marked=21 cached=18
[2026-10-05 18:22:45.895] [info] [t:17320] [llm_client.cpp:76] explaining 1 item(s) on channel 'sentence' preset 'translate' with 'deepseek-flash'
[2026-10-05 18:22:55.730] [warning] [t:17320] [:] qt: Retrying to obtain clipboard.
[2026-10-05 18:22:55.790] [warning] [t:17320] [:] qt: Retrying to obtain clipboard.
[2026-10-05 18:22:55.856] [info] [t:17320] [selection_text_grabber.cpp:417] SelectionTextGrabber::grab: captured 496 characters from 'C:\Program Files\Google\Chrome\Application\chrome.exe'
[2026-10-05 18:22:55.856] [info] [t:17320] [app_controller.cpp:235] selection of 496 character(s): sentence channel
[2026-10-05 18:22:56.726] [info] [t:17320] [known_store.cpp:185] settings saved: level=2 lang=en marked=21 cached=18
[2026-10-05 18:22:56.742] [info] [t:17320] [llm_client.cpp:76] explaining 1 item(s) on channel 'sentence' preset 'translate' with 'deepseek-flash'
[2026-10-05 18:23:05.053] [warning] [t:31924] [:] qt: stream 5 error: "Connection closed"
[2026-10-05 18:23:05.053] [warning] [t:31924] [:] qt: stream 5 finished with error: "HTTP/2 protocol error"
[2026-10-05 18:23:05.053] [warning] [t:31924] [:] qt: stream 3 error: "Connection closed"
[2026-10-05 18:23:05.053] [error] [t:17320] [llm_client.cpp:86] request failed before a response: HTTP/2 protocol error
[2026-10-05 18:23:05.053] [warning] [t:31924] [:] qt: stream 3 finished with error: "HTTP/2 protocol error"
[2026-10-05 18:23:05.055] [info] [t:17320] [app_controller.cpp:377] notice (error): 'The first command of a session auto-starts the browser daemon, and that
daemon inherits the command's stdout while living up to an hour. A caller
that reads stdout to EOF — the Bash tool, `| cat`, `$(...)`, a captured
`&&` chain — keeps waiting long after the command printed its answer and
exited, so a cold start is indistinguishable from a permanent freeze. It
recurs whenever no daemon is running: first call of a session, and after
`close`, `close --all`, a crash, or the idle timeout.'
[2026-10-05 18:23:05.139] [error] [t:17320] [llm_client.cpp:86] request failed before a response: HTTP/2 protocol error
[2026-10-05 18:23:05.141] [info] [t:17320] [app_controller.cpp:377] notice (error): 'The first command of a session auto-starts the browser daemon, and that
daemon inherits the command's stdout while living up to an hour. A caller
that reads stdout to EOF — the Bash tool, `| cat`, `$(...)`, a captured
`&&` chain — keeps waiting long after the command printed its answer and
exited, so a cold start is indistinguishable from a permanent freeze. It
recurs whenever no daemon is running: first call of a session, and after
`close`, `close --all`, a crash, or the idle timeout.'
[2026-10-05 18:23:32.303] [warning] [t:21716] [:] qt: DirectWrite: CreateFontFaceFromHDC() failed (指示输入文件 (例如字体文件) 中的错误。) for QFontDef(Family="MS Sans Serif", pointsize=8.25, pixelsize=13, styleHint=5, weight=400, stretch=100, hintingPreference=0) LOGFONT("MS Sans Serif", lfWidth=0, lfHeight=-13) dpi=96
```