# 覆盖率提升计划

## 1、从哪里出发

读数出自同一天的两份运行结果，原始数据见 [`docs/metrics/coverage-2026-10-03.md`](metrics/coverage-2026-10-03.md)：

| 线 | 口径 | 读数 | 原始数据 |
| --- | --- | ---: | --- |
| C++ | `src/` 下被测试目标链进去的可执行行 | 998/1511 = 66.0% | `test/records/coverage/coverage.xml` |
| QML | 被测试跑到的绑定 / signal handler / 函数位置 | 589/733 = 80.4% | `test/records/qmlcov/qml-coverage.json` |

**两个数不可比，也不许平均。**一个是机器码的行，一个是 QML 表达式的求值位置；分母的构造方式完全不同。

本计划只做一件事：**优先补异常与边界路径**。理由不是 “覆盖率数字好看”，而是现在没被执行到的行里，错误分支占的比例高得不正常——`llm_client.cpp` 的 9 条 HTTP 错误消息一条没跑过，`selection_text_grabber.cpp` 的四条失败分支全空，`core/log.cpp` 三条错误路径全空。这些正是出问题时唯一能救场的东西。

## 2、先记住三个会误导判断的噪声源

动手之前先认这三条，否则会把噪声当成缺口去补：

1. **`lens_gtest_integration` 有一条稳定的红例**（`SelectionHook.ReportsASynthesisedDragAtItsReleasePoint`），它让 `mouse_selection_hook.cpp` 的读数在 80/88/97 行之间摆，总量跟着摆动约 1.1 个百分点。补覆盖率之前先修它，否则每轮读数都在飘。—— 这份计划里没有把它算作缺口，因为它是用例坏了，不是代码没测。
2. **QML 的分子依赖构建树是新的**。QML 编译进二进制，改了 `.qml` 但没重新链接测试目标，量到的就是旧模块。本轮真踩过一次：一次覆盖率运行里 `lens_qtest_surfaces` 报了 4 条 `Cannot assign to non-existent property "contentActive"`，`TrayMenu.qml` 读了 19%；重新构建后同一条用例全绿，那个文件变成 52%。
3. **无头跑 QML 不会渲染，因此 `qsTr()` 标签的绑定永远不会被求值**。QML 未执行的 144 个位置里有 37 个（26%）是这一条造成的，集中在 `SettingsPopup.qml`（13 个）和 `CostPopup.qml`（6 个）。这不是缺口，是口径——除非我们想为文案单独建一条断言渲染的用例，那不值得。

## 3、C++：按性价比排序的补测清单

### 3.1 纯函数里的错误表与边界（最便宜，先做）

这些不需要网络、不需要窗口、不需要真机，只需要把它们放进能被直接调用的位置。

| 目标 | 现状 | 要做的事 |
| --- | --- | --- |
| `llm_client.cpp` 的 `httpErrorFor()` | 9 条错误消息（400/401/402/422/429/500/503/默认）一条都没执行 | 它现在是文件内的匿名命名空间函数，测试调不到。`llm_client.h` 的头注释已经写明“request body 与 response check 住在 `llm_pure.h`，好让它们不用网络就能测”，这张表属于同一类，把它搬到 `llm_pure.cpp`，那里已经测到 93.7% |
| `app_controller.cpp` 的时间与统计口径（约 507–553 行） | “今天 / 昨天”的分支、月份累计、周累计、日均都没跑 | 抽成纯函数（输入时间戳与金额序列，输出文案与数字），单独建用例。这类边界最容易错：跨天、跨月、缺数据 |
| `core/log.cpp` 的三条错误路径 | `from_str` 拿到非法级别名、建日志目录失败、`std::exception` 捕获 | 全部可以离线构造：给一个非法级别字符串、给一个不可写的目录路径。现在 23/31，补完应当接近满 |

### 3.2 真机边界（进 `lens_gtest_integration`，人工执行）

`selection_text_grabber.cpp` 只跑了 72/196。缺的几乎全是失败分支：

- `ClipboardSnapshot::take`：`OpenClipboard` 失败、句柄不是内存（`is a handle, not memory`）、`GlobalUnlock` 分支。
- `ClipboardSnapshot::restore`：`OpenClipboard` 失败、`GlobalAlloc` 失败、写失败后 `GlobalFree`。
- `grab()`：重入守卫、没有前台窗口、`sendCopyKeystroke` 失败、剪贴板在 250 ms 内没变化、取到空文本。
- `readClipboardPrivateFormat` / `foregroundProcessName`：`WideCharToMultiByte` 截断与失败、`OpenProcess` 失败。

这一组是**错误分支密度最高的地方**，而且大多可以靠 “故意制造失败” 自动触发（先占住剪贴板、把前台窗口设成自己、用一个没注册的私有格式名回读）。不必等真机人工拖选。建议先把其中不依赖人手的那几条从 `SelectionGrab.CapturesTheSelectionAndPutsTheClipboardBack` 里拆出来，让它们默认就跑。

### 3.3 需要先加接缝

| 目标 | 现状 | 接缝 |
| --- | --- | --- |
| `llm_client.cpp` 的 `explain()` 主体（55/63 未跑） | `LlmClient` 自己 `new QNetworkAccessManager`，测试无法注入假回复，所以状态码分支（`status != 200`）、`readAll` 为空、`words.size() > kMaxWords` 的边界都测不到 | 给构造函数加一个可选的 `QNetworkAccessManager`（默认自己造），测试传一个覆写了 `createRequest` 返回桩 `QNetworkReply` 的子类。这是 Qt 侧测网络的标准做法，改动只在构造函数 |
| `app_controller.cpp`（136/312） | 没有任何测试目标碰得到它的编排：选区回流、缓存命中、通知、四组设置 setter、`mark()` | 表面目标（`lens_qtest_surfaces`）已经能构造 Controller 并驱动表面，把断言从“表面摆放对了”扩到“Controller 的状态与信号对了”。`SendConfirm`、`selectionBarRequested` 这几条链路上已经有现成的驱动方式 |

## 4、QML：把 “真缺口” 和 “口径缺口” 分开

### 4.1 真缺口（测试没点到的手）

- `TrayMenu.qml` 41/79：整个语言下拉（约 200–254 行）一次没展开过，`onPicked` 三条（统计 / 设置 / 退出）一次没按过。这是本轮 QML 最大的单块缺口。
- `SettingsPopup.qml` 94/116：`Save`、`Defaults` 两个按钮，以及级别 / 语言 / 主题 / 选区捕获四个 setter 的 `onPicked`。
- `Main.qml` 15/30：`showCost` / `showWords` / `showTrayMenu` 三个函数，以及五个 `onXxxRequested` 处理器——也就是 “从托盘菜单进到各个面板” 这条主链路。
- `Bubble.qml` 80/98：`place` / `show` / `updateCountdown`，以及 `mark(word, true)` / `mark(word, false)` 两个按钮。
- 各组件里的 `place()` 与 `onActiveChanged`（`ShadowCard`、`SelectionBar`、`WordsPopup`）：这是同一套 mover 机制，在四个文件里各缺一份，补一个就能照抄。

### 4.2 口径缺口（不是测试缺口，别去补）

- 37 处 `qsTr()` 标签（见第 2 节第 3 条）。
- `Icon.qml:16`、`MenuRow.qml:17`、`ShadowCard.qml:80` 这类 `property alias` / 默认值绑定：只在被外部读取时求值，且多数由 `component.onCompleted` 之外的路径触发。
- `Segment.qml:36`、`DropdownField.qml:115`、`SelectionBar.qml:138` 的 `delegate`：只有 delegate 真的实例化了才会命中，当前的用例没给到那么多项。

## 5、顺序

1. **修掉 `SelectionHook` 那条红例**。它是所有读数抖动的来源，排在一切之前。
2. **搬 `httpErrorFor` 进 `llm_pure.cpp` 并补用例**，同时补 `core/log.cpp` 的三条错误路径。两件都是离线、半天内能落地、直接抬高两个最空的文件的读数。
3. **给 `LlmClient` 加网络接缝**，把状态码与空响应的分支补上。这是当前最大的 “错误路径完全没测” 的单点。
4. **拆 `selection_text_grabber.cpp` 里不依赖人手的失败分支**，让它们进默认运行。
5. **补 `TrayMenu` 语言下拉与三个 `onPicked`**，顺带把 `Main` 的 `showXxx` 链路带出来。
6. 把 `app_controller.cpp` 的断言从表面扩到控制器状态。

## 6、明确不做

- **不设覆盖率阈值**。理由写在 `.github/workflows/ci.yml` 的注释里：同一棵不改的树，总量会在 66.0%–66.6% 之间自己摆，阈值会卡在天气上。要做趋势就按日期存报告比。
- **不为 `profile.cpp` 的 19% 补测试**。那是 `LENS_ENABLE_PROFILE` 在这棵树上关着造成的，要在开着的树上量才有意义。
- **不为 `qsTr()` 标签补渲染用例**。收益是给文案建一份会腐烂的快照，成本是一条需要真实渲染的用例。
- **不为 `main.cpp` / `qt_log.cpp` 补测试**。它们只进 `lens.exe`，任何单元测试目标都链不到；要覆盖得靠真机跑一遍应用，那是 `scripts/ui-*.ps1` 那条线的事。

## 7、怎么核对做完了

每补完一项，跑：

```bash
sh scripts/coverage.sh            # C++，写 test/records/coverage/
sh scripts/qml-coverage.sh        # QML，写 test/records/qmlcov/
```

两个脚本都会在最后打出一行汇总。**跑之前先确认构建树是新的**（第 2 节第 2 条），改过 `.qml` 或 `.cpp` 之后先 `scripts/build.bat`。
