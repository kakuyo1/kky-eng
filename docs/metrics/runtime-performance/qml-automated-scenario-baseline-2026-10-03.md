# QML 自动化场景运行时基线

## 1、采样信息

| 项目 | 内容 |
| --- | --- |
| 应用 | Lens |
| 构建 | `RelWithDebInfo`，启用 `LENS_QML_PROFILE_SCENARIO` |
| Trace | [`qml-scenario-2026-10-03.qtd`](../../../test/records/qml-scenario-2026-10-03.qtd) |
| 自动化方式 | 应用内部调用 QML surface 入口，不移动鼠标、不点击托盘坐标 |
| 场景 | 托盘菜单、语言列表、统计、费用、单词、设置、气泡 |
| Trace wall duration | 8.264 秒 |
| 事件数 | 1434 |

场景由 `scripts/qml-profile.ps1 -Scenario` 启动。应用内部按固定时间线执行 8 个步骤，最后调用 `Qt.quit()`，因此这份 trace 可以重复生成，不依赖桌面坐标和当前鼠标位置。

## 2、事件类型汇总

| 类型 | 事件数 | 捕获耗时合计（ms） |
| --- | ---: | ---: |
| `Creating` | 276 | 763.720 |
| `Javascript` | 604 | 179.554 |
| `Compiling` | 20 | 70.233 |
| `Binding` | 470 | 30.195 |
| `HandlingSignal` | 1 | 0.045 |
| `MemoryAllocation` | 3 | 0 |
| `PixmapCache` | 55 | 0 |
| `SceneGraph` | 4 | 0 |

不同 profiler range 可能重叠，捕获耗时只用于同类热点排序，不能相加成 wall-clock 时间。

## 3、主要热点

### 3.1 语言列表创建

位置：[`TrayMenu.qml:199`](../../../src/app/qml/TrayMenu.qml#L199) 和 [`TrayMenu.qml:212`](../../../src/app/qml/TrayMenu.qml#L212)。

| 类型 | 对象 | 次数 | 合计耗时 |
| --- | --- | ---: | ---: |
| `Creating` | `QtQuick/Repeater` | 2 | 343.991 ms |
| `Creating` | `QtQuick/Text` | 4 | 343.591 ms |

这条热点在静默场景中仍然出现，说明语言列表的 delegate 创建值得单独优化或进一步拆分测量。当前列表只有两个条目，但创建和文本初始化的捕获耗时较高。下一步应确认这部分耗时是否包含第一次 surface 创建、绑定求值或阴影窗口初始化，而不是立即替换 `Repeater`。

### 3.2 自动化场景函数自身

位置：[`Main.qml:138`](../../../src/app/qml/Main.qml#L138)。

`profileScenario` 共 8 次，JavaScript 捕获耗时约 63.691 ms。该函数只是自动化驱动器，不是产品用户路径；它的耗时不应作为应用质量热点。它存在的目的，是让相同场景可以在没有鼠标输入的情况下重复采样。

### 3.3 面板定位

位置：[`Main.qml:82`](../../../src/app/qml/Main.qml#L82)。

`placePanel` 5 次，JavaScript 捕获耗时约 36.409 ms。它会调用 `placeBeside`，写入窗口坐标，显示窗口并调用 `raise()`。后续应在不包含自动化函数本身的场景中单独测量该路径。

### 3.4 面板和气泡创建

其他值得记录的捕获成本：

- `Main.qml:14` 的 `Window` 创建：约 18.761 ms。
- `Main.qml:118` 的 `showSettings`：约 11.871 ms。
- `Main.qml:130` 的 `showWords`：约 11.157 ms。
- `Bubble.qml:53` 的 `show`：约 10.905 ms。
- `Icon.qml:23` 的图片创建：46 次，约 8.584 ms。

这些数值目前不足以证明需要重构，但可以作为后续场景拆分时的对照点。

## 4、结论

1. QML profiler 场景已经可以完全自动运行，不需要鼠标或托盘点击。
2. `TrayMenu` 语言列表的 `Repeater` 和 `Text` 创建是当前自动化场景的首要热点。
3. 面板定位和窗口创建是次级成本。
4. 当前场景没有可靠的逐帧百分位数，不能据此判断 60 Hz 卡顿。
5. `profileScenario` 的成本属于测试驱动器，不应混入产品性能结论。

## 5、复现命令

```powershell
./scripts/qml-profile.ps1 `
  -BuildDir build-ninja-qml-profile `
  -Output test/records/qml-scenario-2026-10-03.qtd `
  -Scenario
```

解析 trace：

```powershell
python scripts/parse-qml-trace.py `
  --trace test/records/qml-scenario-2026-10-03.qtd `
  --output test/records/qml-scenario-2026-10-03.json
```

> AI assistance has been used to create this output.
