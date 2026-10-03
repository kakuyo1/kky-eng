# 托盘菜单正文延迟加载实验

## 结果

`TrayMenu` 的 `Window`、信号和定位路径保持不变，正文的四个 `MenuRow` 改为首次打开时通过 `Loader` 创建，此后保持加载。语言列表仍按展开状态加载。

| 场景 | 修改前 | 修改后 |
| --- | ---: | ---: |
| `startup`：`Creating` 事件数 | 270 | 252 |
| `startup`：`PixmapCache` 事件数 | 55 | 35 |
| `startup`：`MenuRow.qml:47` 的 `Text` 创建 | 8 次，385.622 ms | 0 次 |
| `tray`：`MenuRow.qml:47` 的 `Text` 创建 | 8 次，359.906 ms | 8 次，0.188 ms |
| `startup`：trace wall duration | 2026.217 ms | 2065.255 ms |

修改后，启动场景的最大热点移到 `SendConfirm.qml:109` 的 `Text` 创建（2 次，435.374 ms）。这表明之前归在 `MenuRow` 的大部分成本属于共享的首次文本初始化，而非该组件每次创建的成本。**本次实验没有证明启动墙钟时间缩短**；它验证的是菜单对象创建移出了启动阶段。`tray` 场景修改前后的 wall duration 分别为 3852.366 ms 和 3877.918 ms，也不能据单次运行判断首次打开速度。

Qt QML profiler 的区间可能重叠，表内的事件耗时不可相加为墙钟耗时。当前 trace 没有可靠的逐帧百分位数。

## 验证

`qmllint --json`：18 个文件，0 诊断；AST：0 解析错误；`lens_qtest_components` 和 `lens_qtest_surfaces` 退出码均为 0。菜单正文首次打开后可见；语言列表的命中矩形由表面测试覆盖。真实桌面截图见 [`qml-tray-visual4.png`](../../../test/records/qml-tray-visual4.png)（本地记录，不提交）。

Trace：[`startup` 修改前](../../../test/records/qml-startup-next.qtd)、[`tray` 修改前](../../../test/records/qml-tray-next.qtd)、[`startup` 修改后](../../../test/records/qml-startup-optimized2.qtd)、[`tray` 修改后](../../../test/records/qml-tray-optimized2.qtd)。这些本地记录被 git 忽略。
