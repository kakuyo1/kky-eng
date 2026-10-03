# 语言列表延迟加载验证

## 1、范围

本次采样验证 `TrayMenu.qml` 的语言列表从隐藏但预创建改为 `Loader.active: listVisible` 后的运行时行为。场景仍由应用内部自动触发，不移动鼠标。

| 场景 | Trace wall duration | 主要用途 |
| --- | ---: | --- |
| `startup` | 1.980 秒 | 验证启动不创建隐藏语言列表 |
| `tray` | 3.860 秒 | 验证展开托盘菜单和语言列表时的创建成本 |

Trace 文件：

- [`qml-startup-loader.qtd`](../../../test/records/qml-startup-loader.qtd)
- [`qml-tray-loader.qtd`](../../../test/records/qml-tray-loader.qtd)

## 2、启动场景

事件耗时汇总：

| 类型 | 事件数 | 捕获耗时合计 |
| --- | ---: | ---: |
| `Creating` | 270 | 447.984 ms |
| `Compiling` | 20 | 69.538 ms |
| `Binding` | 463 | 24.057 ms |
| `Javascript` | 583 | 14.436 ms |

启动热点：

- [`MenuRow.qml:47`](../../../src/app/qml/components/MenuRow.qml#L47) 的 `Text` 创建：357.700 ms。
- [`Main.qml:0`](../../../src/app/qml/Main.qml#L1) 的 QML compiling：49.380 ms。
- [`MenuRow.qml:55`](../../../src/app/qml/components/MenuRow.qml#L55) 的 `Text` 创建：15.580 ms。
- [`Main.qml:14`](../../../src/app/qml/Main.qml#L14) 的 `Window` 创建：14.390 ms。

语言列表的 `Repeater` 和其两个语言选项的 `Text` 不再出现在启动场景热点中，说明 Loader 已阻止该隐藏子树在启动时创建。

## 3、托盘场景

事件耗时汇总：

| 类型 | 事件数 | 捕获耗时合计 |
| --- | ---: | ---: |
| `Creating` | 278 | 518.628 ms |
| `Compiling` | 20 | 79.068 ms |
| `Javascript` | 599 | 37.021 ms |
| `Binding` | 471 | 27.597 ms |

托盘场景热点：

- [`MenuRow.qml:47`](../../../src/app/qml/components/MenuRow.qml#L47) 的 `Text` 创建：408.550 ms。
- [`Main.qml:0`](../../../src/app/qml/Main.qml#L1) 的 QML compiling：58.100 ms。
- [`Main.qml:14`](../../../src/app/qml/Main.qml#L14) 的 `Window` 创建：17.450 ms。
- [`MenuRow.qml:55`](../../../src/app/qml/components/MenuRow.qml#L55) 的 `Text` 创建：17.440 ms。

语言列表的创建成本已经转移到托盘场景，而不是启动阶段。这是预期的生命周期变化；下一步应确认 `MenuRow.qml:47` 的共享文本创建是否来自所有菜单项的启动预创建，还是来自托盘菜单首次显示。

## 4、结论

1. `TrayMenu` 语言列表的 Loader 修复生效，启动阶段不再创建隐藏语言列表。
2. 当前启动主要成本集中在菜单行文本和所有 surface 的预创建。
3. 下一候选是评估整个 `TrayMenu` 是否也应由 `Loader` 延迟到首次托盘打开，但这需要先确认托盘打开路径和窗口定位契约。
4. 当前没有可靠的逐帧百分位数，不能据此判断帧卡顿。

## 5、复现命令

```powershell
./scripts/qml-profile.ps1 -BuildDir build-ninja-qml-profile `
  -Output test/records/qml-startup-loader.qtd -Scenario startup

./scripts/qml-profile.ps1 -BuildDir build-ninja-qml-profile `
  -Output test/records/qml-tray-loader.qtd -Scenario tray
```

> AI assistance has been used to create this output.
