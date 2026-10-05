# QML 用户流程运行时基线

## 1、采样信息

| 项目 | 内容 |
| --- | --- |
| 应用 | Lens |
| Qt | 6.9.0 |
| 构建 | `RelWithDebInfo`，启用 `LENS_ENABLE_QML_DEBUG` |
| Trace | [`qml-full-2026-10-03.qtd`](../../../test/records/qml-full-2026-10-03.qtd) |
| 采样方式 | 启动、托盘、设置、统计、单词、气泡和拖动场景，各执行约 3 轮 |
| Trace wall duration | 82.786 秒 |
| eventData totalTime | 1172.393 ms |
| 事件数 | 1477 |

`eventData totalTime` 与各类事件耗时合计不是 wall-clock 时间。不同 profiler range 可能重叠，事件耗时只用于比较热点，不能直接相加后当作用户等待时间。

本次 trace 没有形成可靠的逐帧 p50/p95/p99 数据。采样中有 10 个 animation frame 段，但 framerate 分段包含 6、62、500 和 1000 等值，不能据此推导稳定的 60 Hz 帧时间基线。

## 2、事件类型汇总

| 类型 | 事件数 | 捕获耗时合计（ms） | 平均每事件（ms） |
| --- | ---: | ---: | ---: |
| `Javascript` | 625 | 1743.688 | 2.790 |
| `Creating` | 276 | 1293.804 | 4.688 |
| `HandlingSignal` | 22 | 935.423 | 42.519 |
| `Compiling` | 20 | 88.902 | 4.445 |
| `Binding` | 470 | 36.895 | 0.079 |
| `MemoryAllocation` | 3 | 0 | 0 |
| `PixmapCache` | 55 | 0 | 0 |
| `SceneGraph` | 4 | 0 | 0 |

原始事件数会随运行时间和操作次数增长，不适合作为主要性能指标。当前最值得关注的是 `Creating`、`Javascript` 和 `HandlingSignal` 的耗时及其 source location。

## 3、前五个热点

### 3.1 托盘语言列表的对象创建

位置：[`TrayMenu.qml:199`](../../../src/app/qml/TrayMenu.qml#L199) 和 [`TrayMenu.qml:212`](../../../src/app/qml/TrayMenu.qml#L212)。

| 类型 | 位置 | 次数 | 合计耗时 |
| --- | --- | ---: | ---: |
| `Creating` | `Repeater` | 2 | 598.132 ms |
| `Creating` | `Text` | 4 | 597.503 ms |

这里的 `Repeater` 只有两个语言选项，但 trace 将 delegate 和其 `Text` 子项的创建归因到这两处，并出现约 598 ms 的捕获耗时。这个数字足以成为第一优先级，但还不能直接证明 `Repeater` 本身是根因；创建过程中可能包含 delegate 初始化、绑定求值或窗口布局。

下一步应只打开和关闭托盘菜单，单独采样 3 轮，确认这两个创建热点是否稳定出现。若稳定，再比较把语言列表延迟到首次展开、减少 delegate 初始绑定或拆分阴影/窗口创建的效果。

### 3.2 面板显示路径

位置：[`Main.qml:82`](../../../src/app/qml/Main.qml#L82)。

`placePanel` 共 27 次，JavaScript 捕获耗时约 203.040 ms，平均约 7.520 ms。函数本身包含定位、两个窗口坐标赋值、显示和 `raise()`；它还会间接触发表面布局。

这不是单纯的 JavaScript 算法热点。应在窄场景中分别测量 `placeBeside`、`panel.visible` 和 `panel.raise()` 产生的后续创建与绑定事件，再决定是否需要优化。

### 3.3 `StatRow` 点击信号

位置：[`StatRow.qml:65`](../../../src/app/qml/components/StatRow.qml#L65)。

`HandlingSignal` 和对应的 JavaScript handler 各约 116 ms，6 次，平均约 19.37 ms。源码中的 handler 只是调用 `root.tapped()`，因此这里的耗时更可能包含信号传播和下游面板切换，而不是这一行本身。

不要直接重写 `StatRow`。应对统计页面的 `costRequested` 和 `wordsRequested` 分开采样，确认耗时属于 signal delivery、面板创建还是绑定重算。

### 3.4 `Main.qml` 的面板导航

位置：[`Main.qml:111`](../../../src/app/qml/Main.qml#L111)。

`showStats` 9 次，JavaScript 捕获耗时约 114.615 ms，平均约 12.735 ms。它调用 `hidePanels` 和 `placePanel`，所以属于导航链路的聚合热点。对应的 [`Main.qml:102`](../../../src/app/qml/Main.qml#L102) `hidePanels` 也有约 85.077 ms，27 次。

建议在一次统计页面导航中采样完整调用链，而不是把 `showStats` 和 `hidePanels` 分别当作独立根因。

### 3.5 `MenuRow` 点击信号

位置：[`MenuRow.qml:33`](../../../src/app/qml/components/MenuRow.qml#L33)。

`HandlingSignal` 和对应 JavaScript handler 各约 110 ms，8 次，平均约 13.78 ms。与 `StatRow` 相同，这一行只调用 `root.picked()`，耗时应视为菜单操作链路的入口归因。

下一轮应把托盘菜单的点击项分成 `打开设置`、`打开统计`、`切换语言`、`退出` 分别测量，确认是否只有面板打开路径昂贵。

## 4、当前结论

1. 当前最大可疑成本在托盘语言列表的创建阶段，而不是 QML JavaScript 圈复杂度。
2. `Main.qml` 的定位和面板导航是第二类热点，但平均单次耗时低于语言列表创建热点。
3. `StatRow`、`MenuRow` 的 signal handler 行号是调用链归因点，不能直接当成组件实现缺陷。
4. 当前 trace 没有可靠的帧时间百分位数，因此不能据此判断是否存在 60 Hz 卡顿。
5. 静态 AST 显示的深度 8 和绑定数量暂时没有被证明是运行时瓶颈。

## 5、下一轮采样

按以下顺序做窄场景 trace，每个场景 3 轮：

1. 只启动并退出，不打开任何面板，测启动创建和编译成本。
2. 只打开、关闭托盘菜单，测 `TrayMenu` 的 `Repeater` 和 `Text` 创建。
3. 只打开统计页面并进入费用、单词页面，测 `StatRow`、`showStats` 和面板创建。
4. 只做一次气泡显示和拖动，测 `Bubble` 的创建、绑定和 scenegraph。

当前基线解析由 [`parse-qml-trace.py`](../../../scripts/profiling/parse-qml-trace.py) 完成：

```powershell
python scripts/profiling/parse-qml-trace.py `
  --trace test/records/qml-full-2026-10-03.qtd `
  --output test/records/qml-full-2026-10-03.json
```

## 6、限制

- Trace 是 Qt profiler 1.02 XML；本报告统计的是带 `duration` 的 profiler ranges。
- range 耗时可能重叠，不能相加成 wall-clock 时间。
- 当前 parser 尚未从 animation frame 段推导可靠的 frame-time 百分位数。
- `qmllint`、AST 和 profiler 分别回答语义正确性、静态结构和运行时行为，三者不能互相替代。

> AI assistance has been used to create this output.
