# 测试覆盖率基线（2026-10-05）

这一份盖两条线：`src/` 下的 C++ 行，与 QML 模块里被求值的表达式位置。两条线的工具、口径、分母构造
方式都不同，**读数不可比，也不许平均**。要读的是 “数的是什么”，见
[`what-coverage-counts-2026-10-05.md`](what-coverage-counts-2026-10-05.md)；要补哪一块，见
[`coverage-plan-2026-10-03.md`](coverage-plan-2026-10-03.md)。

本报告取代 [`../code-quality/test-coverage-baseline-2026-10-03.md`](../code-quality/test-coverage-baseline-2026-10-03.md)：
那一份是 2026-10-03 的快照，早于波一到波四的构建改动，**留作历史，不改数字**。两份的分母不可直接比——
基线之后 QML 模块多了 `Notice.qml` 与 `theme/`，C++ 侧多了 `autostart`、`system_motion`、`notice.h`。

## 1、报告信息

| 项目 | C++ 一条线 | QML 一条线 |
| --- | --- | --- |
| 分析范围 | `src/` 下被测试目标链进去的源文件与头文件 | `src/app/qml/` 下的模块文件 |
| 工具 | `OpenCppCoverage 0.9.9.0` | `qmlprofiler`（Qt 6.9.0）+ `tree-sitter-qmljs` |
| 采集方式 | 对 `build-ninja` 已有的 Debug 二进制逐个插桩，五份结果合并成一份 Cobertura XML | 两个 QTest 目标各跑一遍 QML profiler，取 trace 里出现过的 `{文件, 行}`，除以解析树里的可求值位置数 |
| 单位 | 机器码行 | 绑定 / signal handler / 函数的位置 |
| 原始数据 | `test/records/coverage/coverage.xml` | `test/records/qmlcov/qml-coverage.json` |
| 构建树 | 两者都是 `build-ninja`（Debug），都不新建树、不改构建标志 | 同左 |

本报告只回答 “哪些东西被测试跑过”，不回答 “跑过的结果对不对”。

## 2、执行摘要

C++ 行覆盖 **75.7%（1408/1860，26 个文件）**；QML 执行覆盖 **86.6%（738/852）**。

两个数都比 2026-10-03 那份高，但**差值不能直接读成进步**——分母跟着源码长过。可比的是同一口径下的
文件：`llm_client.cpp` 从 12.7% 到 98.4%，`log.cpp` 从 74.2% 到 100%，`TrayMenu.qml` 从 52% 到 91%，
`Main.qml` 从 50% 到 83%。这些是这一轮补测直接抬起来的。

三条口径仍然成立，读表前先认下：

- **没报出来的文件不是 0%**：只被 `lens.exe` 引用的文件不进测试二进制的链接行，声明与宏居多的头文件
  没有可执行行。
- **`profile.cpp` 的 19.0% 是构建配置**：`LENS_ENABLE_PROFILE` 在这棵树上关着。
- **无头跑不渲染，`qsTr()` 标签绑定永不被求值**：QML 未执行的位置里约四分之一是这一条。

## 3、C++ 一条线

### 3.1 五个目标

`scripts/quality/coverage.sh` 依次覆盖能无人值守跑完的五个目标。本轮用例数与结果：

| 目标 | 用例数 | 结果 |
| --- | ---: | --- |
| `lens_gtest_unit` | 59 | 59 绿 |
| `lens_gtest_perf` | 1 | 绿 |
| `lens_gtest_integration` | 17 | 16 绿、1 skip、0 红 |
| `lens_qtest_components` | 51 | 51 绿、1 skip |
| `lens_qtest_surfaces` | 71 | 71 绿、3 skip |

`lens_gtest_smoke` 不在内：它要 `settings.local.json` 里的 key，且会花钱。

**2026-10-03 那条稳定红例已修**：`SelectionHook.ReportsASynthesisedDragAtItsReleasePoint` 现在绿，且快
（约 60–270 ms，此前要等满 2000 ms 超时）。因此 `mouse_selection_hook.cpp` 的读数不再摆动，本轮两次
采集逐位相同（§6）。

`lens_gtest_integration` 的 skip 来自两条要人手的路径：取词往返（`LENS_HOOK_SMOKE=1` 才跑），与
“把前台窗口换成自己”（取不到前台时跳过，故默认有时是 15 绿 2 skip）。

### 3.2 按目录

| 模块 | 文件数 | 覆盖行 | 可执行行 | 行覆盖率 |
| --- | ---: | ---: | ---: | ---: |
| `src/core` | 7 | 563 | 638 | 88.2% |
| `src/llm` | 6 | 364 | 390 | 93.3% |
| `src/app` | 13 | 481 | 832 | 57.8% |
| 合计 | 26 | 1408 | 1860 | 75.7% |

### 3.3 按文件

| 文件 | 覆盖行 | 可执行行 | 行覆盖率 |
| --- | ---: | ---: | ---: |
| [`core/filter_core.cpp`](../../../src/core/filter_core.cpp) | 315 | 334 | 94.3% |
| [`core/known_store.cpp`](../../../src/core/known_store.cpp) | 102 | 110 | 92.7% |
| [`core/stats_store.cpp`](../../../src/core/stats_store.cpp) | 95 | 96 | 99.0% |
| [`core/log.cpp`](../../../src/core/log.cpp) | 31 | 31 | 100.0% |
| [`core/profile.cpp`](../../../src/core/profile.cpp) | 11 | 58 | 19.0% |
| [`core/known_store.h`](../../../src/core/known_store.h) | 3 | 3 | 100.0% |
| [`core/stats_store.h`](../../../src/core/stats_store.h) | 6 | 6 | 100.0% |
| [`llm/llm_pure.cpp`](../../../src/llm/llm_pure.cpp) | 150 | 157 | 95.5% |
| [`llm/llm_pricing.cpp`](../../../src/llm/llm_pricing.cpp) | 47 | 52 | 90.4% |
| [`llm/llm_protocol.cpp`](../../../src/llm/llm_protocol.cpp) | 95 | 108 | 88.0% |
| [`llm/llm_client.cpp`](../../../src/llm/llm_client.cpp) | 62 | 63 | 98.4% |
| [`llm/llm_client.h`](../../../src/llm/llm_client.h) | 7 | 7 | 100.0% |
| [`llm/llm_pricing.h`](../../../src/llm/llm_pricing.h) | 3 | 3 | 100.0% |
| [`app/app_controller.cpp`](../../../src/app/app_controller.cpp) | 232 | 418 | 55.5% |
| [`app/selection_text_grabber.cpp`](../../../src/app/selection_text_grabber.cpp) | 79 | 200 | 39.5% |
| [`app/mouse_selection_hook.cpp`](../../../src/app/mouse_selection_hook.cpp) | 97 | 110 | 88.2% |
| [`app/tray.cpp`](../../../src/app/tray.cpp) | 44 | 61 | 72.1% |
| [`app/autostart.cpp`](../../../src/app/autostart.cpp) | 4 | 18 | 22.2% |
| [`app/system_motion.cpp`](../../../src/app/system_motion.cpp) | 8 | 8 | 100.0% |
| [`app/app_controller.h`](../../../src/app/app_controller.h) | 2 | 2 | 100.0% |
| [`app/selection_text_grabber.h`](../../../src/app/selection_text_grabber.h) | 6 | 6 | 100.0% |
| [`app/mouse_selection_hook.h`](../../../src/app/mouse_selection_hook.h) | 1 | 1 | 100.0% |
| [`app/tray.h`](../../../src/app/tray.h) | 1 | 1 | 100.0% |
| [`app/autostart.h`](../../../src/app/autostart.h) | 3 | 3 | 100.0% |
| [`app/system_motion.h`](../../../src/app/system_motion.h) | 1 | 1 | 100.0% |
| [`app/notice.h`](../../../src/app/notice.h) | 3 | 3 | 100.0% |

头文件的 100% 是行数极少的结果（多为一两个内联体），不要读成 “这块已经被测透了”。

三处仍然读得低的，成因各自不同：

- `app_controller.cpp` 55.5%：选区回流那一整条（`beginSelection` → `explain` → `showBubble` /
  `notice`）的入口不是 `Q_INVOKABLE`，钩子也没暴露给 QML，测试目标碰不到；要覆盖它得靠真桌面。
- `selection_text_grabber.cpp` 39.5%：抓取主路径要真的有别的窗口当前台，默认 skip。不依赖人手的失败
  分支这一轮拆进来了（句柄格式跳过、空快照的 no-op），其余仍要人手。
- `autostart.cpp` 22.2%：写注册表是真机状态，离线用例只读断言，写入归人工。

### 3.4 没有行数据的文件

`app/main.cpp`、`llm/qt_log.cpp` 在 Cobertura 里不出现，因为它们只被 `lens.exe` 引用，静态链接不会把
没人引用的目标文件拉进测试二进制；其余头文件（`core/filter_core.h`、`core/log.h`、`core/profile.h`、
`llm/llm_pure.h`、`llm/llm_protocol.h`、`llm/qt_log.h`）是声明与宏，没有可执行行。**都不是 0%。**

## 4、QML 一条线

### 4.1 总数

**738/852 = 86.6%**，由 `lens_qtest_components` 与 `lens_qtest_surfaces` 两个目标共同跑出。事件计数：
`Binding` 2586、`HandlingSignal` 92、`Javascript` 3432、其他 0；字面量绑定 392 个两侧都不算。

分母从 2026-10-03 的 733 长到 852，是波一到波四给模块加了文件（`Notice.qml`、`theme/`、`MixedText.qml`）
的缘故，不是口径变了。

### 4.2 按文件

| 文件 | 执行 | 可求值位置 | 覆盖率 |
| --- | ---: | ---: | ---: |
| [`theme/Motion.qml`](../../../src/app/qml/theme/Motion.qml) | 5 | 5 | 100% |
| [`theme/Dark.qml`](../../../src/app/qml/theme/Dark.qml) | 2 | 2 | 100% |
| [`theme/Light.qml`](../../../src/app/qml/theme/Light.qml) | 2 | 2 | 100% |
| [`components/MixedText.qml`](../../../src/app/qml/components/MixedText.qml) | 13 | 13 | 100% |
| [`components/StatRow.qml`](../../../src/app/qml/components/StatRow.qml) | 28 | 28 | 100% |
| [`components/Switch.qml`](../../../src/app/qml/components/Switch.qml) | 13 | 13 | 100% |
| [`components/MenuRow.qml`](../../../src/app/qml/components/MenuRow.qml) | 18 | 19 | 95% |
| [`components/DropdownField.qml`](../../../src/app/qml/components/DropdownField.qml) | 40 | 43 | 93% |
| [`TrayMenu.qml`](../../../src/app/qml/TrayMenu.qml) | 69 | 76 | 91% |
| [`components/SwitchRow.qml`](../../../src/app/qml/components/SwitchRow.qml) | 9 | 10 | 90% |
| [`components/Segment.qml`](../../../src/app/qml/components/Segment.qml) | 16 | 18 | 89% |
| [`Notice.qml`](../../../src/app/qml/Notice.qml) | 23 | 26 | 88% |
| [`WordsPopup.qml`](../../../src/app/qml/WordsPopup.qml) | 112 | 128 | 88% |
| [`SelectionBar.qml`](../../../src/app/qml/SelectionBar.qml) | 41 | 47 | 87% |
| [`components/Icon.qml`](../../../src/app/qml/components/Icon.qml) | 6 | 7 | 86% |
| [`Bubble.qml`](../../../src/app/qml/Bubble.qml) | 101 | 119 | 85% |
| [`components/Tokens.qml`](../../../src/app/qml/components/Tokens.qml) | 17 | 20 | 85% |
| [`Main.qml`](../../../src/app/qml/Main.qml) | 25 | 30 | 83% |
| [`CostPopup.qml`](../../../src/app/qml/CostPopup.qml) | 36 | 44 | 82% |
| [`SettingsPopup.qml`](../../../src/app/qml/SettingsPopup.qml) | 107 | 132 | 81% |
| [`components/ShadowCard.qml`](../../../src/app/qml/components/ShadowCard.qml) | 26 | 32 | 81% |
| [`StatsPopup.qml`](../../../src/app/qml/StatsPopup.qml) | 29 | 38 | 76% |

三条读这份表的注意事项：

- **未执行的位置里约四分之一是 `qsTr()` 标签**，无头跑不渲染，那不是缺口。
- **这个数看不见分支。**一个绑定只要对象被创建就会求值一次，“两条分支都测过没有” 它答不上来。
- **它依赖构建树是新的。**QML 编进二进制，改了 `.qml` 没重新链接测试目标，量到的就是旧模块。

## 5、这一轮补了什么，还缺什么

补的（按 `coverage-plan-2026-10-03.md` §5 的次序）：

1. 修掉稳定红例，读数的摆动源头消失（§3.1、§6）。
2. `httpErrorFor` 搬进 `llm_pure.cpp` 并补齐八条状态码；`log.cpp` 的三条错误路径全绿（100%）。
3. `LlmClient` 构造函数加了可选 `QNetworkAccessManager`，桩 manager 让状态码、空响应、传输失败三条
   分支离线可测（`llm_client.cpp` 98.4%）。
4. `selection_text_grabber.cpp` 里不依赖人手的失败分支拆进默认运行（句柄格式跳过、空快照 no-op）。
5. `TrayMenu` 的语言下拉与三个 `onPicked`，连带 `Main` 的 `showXxx` 链（`TrayMenu.qml` 52% → 91%，
   `Main.qml` 50% → 83%）。
6. `app_controller.cpp` 的断言从表面扩到控制器状态（导出、无 pending 的动作、空 dismiss、未知剪贴板
   策略、模式标签、只写密钥）。

没做，且是故意的：

- **不设阈值**（`.github/workflows/ci.yml` 的注释）。
- **不为 `qsTr()` 标签、`profile.cpp`、`main.cpp` / `qt_log.cpp` 补测**（计划 §6）。
- **`app_controller.cpp` 的时间与统计口径（约 507–553 行）没抽成纯函数**：计划 §3.1 列了它，但 §5 的
  六步次序里没有它，故留待后续。
- **`selection_text_grabber.cpp` 里要另一个前台窗口的失败分支**（`sendCopyKeystroke` 被拒、拷到空文本、
  250 ms 没变化、剪贴板被占）仍是人工用例：驱动它们要往当前焦点注入一次 `Ctrl+C`，默认运行不许对读者
  自己的前台窗口做这件事。

## 6、复现命令

```bash
sh scripts/quality/coverage.sh            # C++，写 test/records/coverage/
sh scripts/quality/qml-coverage.sh        # QML，写 test/records/qmlcov/
```

`coverage.sh` 默认跑五个能无人值守跑完的目标，也可以只点名几个。CI 只跑其中四个（它没有真鼠标与真
剪贴板，`lens_gtest_integration` 不在内），所以 **CI 的总量会比这里低**——两份分母本来就不同。

C++ 那条线要求 `OpenCppCoverage.exe` 在 `C:\Program Files\OpenCppCoverage\`；QML 那条要求
`node_modules`（`npm ci`）与 Qt 的 `qmlprofiler`。

**稳定性**：同一棵树连跑两次，总量逐位相同（1408/1860），26 个文件无一动。2026-10-03 那份记录的摆动
（`mouse_selection_hook.cpp` 88/97 行）随红例一起消失。QML 那条线仍有小幅浮动（同一构建的两轮 trace
之间事件数会变），本表报的是落在 `test/records/qmlcov/qml-coverage.json` 里的那一次。

读数取自 `build-ninja`，那棵树的内容是分支 `w2-coverage` 的 `4a06c6c` 加上本轮提交前的文档改动。
