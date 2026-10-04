# 空闲内存

## 结果

装出来的 Lens 起来之后不动它，每 30 s 采一次，采 10 分钟。改前是基线包，改后是这份文档旁边那份
`package-size` 报告里减过四项的包：

| 量 | 改前（基线包） | 改后（减过四项） |
| --- | ---: | ---: |
| 私有字节，起 | 155.85 MB | 156.52 MB |
| 私有字节，10 分钟后 | 155.69 MB | 156.50 MB |
| 工作集，起 → 10 分钟后 | 180.64 → 133.46 MB | 180.52 → 129.23 MB |
| 句柄数，起 → 末 | 2782 → 2779 | 2782 → 2778 |
| 线程数，起 → 末 | 80 → 72 | 80 → 73 |

私有字节十分钟里动了 −0.16 MB / −0.02 MB，句柄各掉三四个，都没有单调上涨。工作集那一栏从 180 MB 掉到
130 MB 是 Windows 在回收可丢弃页，不是应用让出来的——私有字节才是应用自己的账。

## 占在哪

进程里映射了 131 个模块，按映射大小排前面的是显卡驱动而不是自己：

| 模块 | 映射 |
| --- | ---: |
| nvgpucomp64.dll / nvwgf2umx.dll（NVIDIA） | 105.7 / 86.1 MB |
| amdxx64.dll（AMD） | 62.5 MB |
| Qt6Core / Qt6Gui | 9.5 / 9.1 MB |
| Qt6Widgets / Qt6Quick / Qt6Qml | 6.2 / 6.1 / 5.0 MB |
| windows.storage / SHELL32 | 8.6 / 7.6 MB |

驱动那几个 DLL 是**共享映射**，不计进私有字节；私有那 156 MB 里，Qt 的代码与数据、QML 引擎（九个窗口在
`Main.qml` 里一次建好）、场景图与字体占大头。应用自己加载的表只有两份，日志里报得出来：词表 88918 条
（`filter_core.cpp` 的 `unordered_map<string, size_t>`）、不规则屈折表 5757 条。

另外有一个一次性的台阶，见同目录的 `memory-leak` 报告：第一个表面真正画出来的那一刻，私有字节 +60 MB、
句柄 +1634、线程 +82，之后不再涨。空闲态（什么都没画）就是上表那 156 MB。

## 为什么这次一项都没减

W1 的文件面是根 `CMakeLists.txt`、`installer/` 与 `docs/metrics/`。空闲内存的大头是 Qt 运行时与 QML
引擎本身，而从包里删掉一个**没被加载**的 DLL 不影响常驻集——`package-size` 报告里减掉的四项，改后的私有
字节是 156.52 MB，与改前的 155.85 MB 在同一水位（差 0.7 MB，在轮次噪声里）。

真正能动它的两处，都不在这个文件面里，也都没有测量支撑能保证收益：

- **词表容器**：`unordered_map<string, size_t>` 存 88918 条，换成有序 `vector` 加二分查找省的是桶数组与
  节点开销，量级几 MB，而它要改的是 `src/core/filter_core.cpp`——T2 那条轨的文件。
- **九个窗口在启动时全建好**：其中八张表面大部分时间不显示，按需建能省下各自的对象与场景图。这是 QML
  改动，波四给每个表面铺了出现动画之后，改成按需建要连带重做那层。

两项都留给拥有那些文件的改动，不在这一步凭感觉动。

## 数字来源

`test/records/w1-idle-memory-before.txt` 与 `w1-idle-memory-after.txt`（gitignored，本机运行记录）：
PowerShell 每 30 s 读一次 `Get-Process` 的 `PrivateMemorySize64` / `WorkingSet64` / `HandleCount` /
`Threads.Count`。测的应用是装出来的那一份（`%LOCALAPPDATA%\Programs\Lens\lens.exe`），启动 15 s 后开始
采样；`selectionCapture` 置为 `false`，否则读者自己的每一次拖选都会掺进来（改前那一次就掺进来了，见
`memory-leak` 报告）。
