# 全量编译基线

## 结果

在 `build-timing` 一次性目录里，对同一个仓库各做一次全量（clean）编译，取墙钟时间：

| 配置 | `jobs=4` | Ninja 默认（本机 18 个并行） |
| --- | ---: | ---: |
| 改前（`all` = 主程序 + 5 个测试目标） | 72 s | 46 s |
| 改后（预编译头 + 满核，目标集不变） | 48 s | 44 s |
| 改后，`all` 只剩主程序（最终状态） | 46 s | 37 s |

同一份 `ninja` 日志里全部边缘的耗时之和（CPU 时间，不随并行度变化）：改前 395 s，加预编译头后 181 s。并行度（CPU 时间除以墙钟时间）从 8.9 倍降到 5 倍左右——总的工作少了一半，串行的关键路径却没变短，于是改后主要受关键路径限制，不再是受核数限制。这也解释了最后一行：`jobs=4` 时测试目标填的是关键路径的空档，去掉它们只省下 2 s；满核时它们才真正与主程序抢核，去掉省下 7 s。

configure 一步约 34 s，两次测量都没有变化。

## 时间花在哪

`ninja` 的日志里，改前各部分的 CPU 时间：

| 部分 | 边缘数 | CPU |
| --- | ---: | ---: |
| QML 提前编译（每个 `.qml` 生成一个 `.cpp` 再编译它） | 59 | 152 s |
| `src/app` | 39 | 67 s |
| `test/googletest` | 11 | 54 s |
| `third_party`（spdlog 与 googletest） | 19 | 31 s |
| AUTOMOC | 36 | 25 s |
| `test/qtest` | 6 | 25 s |
| `src/llm` | 6 | 24 s |
| `src/core` | 6 | 17 s |
| 链接与零碎 | 3 | 1 s |

QML 提前编译一处就占 39%，而成品只有 20 个 `.qml` 文件：每个生成的 `.cpp` 里绝大多数是数据数组，真正花时间的是它重新展开一遍 Qt Qml 的头文件。

## 改动

1. **`CMakePresets.json` 去掉 `jobs`**：原来两个 ninja build preset 都写死 `"jobs": 4`，在这台 16 核机器上留下 12 个核空转。去掉之后 `cmake --build` 不再传 `-j`，Ninja 自己按核数加二取并行度，换一台机器也不用改。峰值内存实测还有约 11 GB 空闲，不构成瓶颈。

2. **`cmake/lens_pch.h` 与 `cmake/lens_pch_quick.h`**：预编译头。每个链接 Qt 的目标各构建一份，合并后能省掉绝大多数重复的头文件解析。分成两个文件是因为 Quick 那一半对只链接 Qt Core 的目标（`lens_llm`、离线 gtest）无法解析。目标名单集中在顶层 `CMakeLists.txt` 末尾，一眼能看全。

   预编译头对 TU 数多的目标才划算：`lens_app` 有 25 个 TU，其中 20 个是 QML 提前编译生成的；`lens`、两个 qtest 目标各只有 1 到 3 个，各付一次约 5 s 的预编译头构建并不明显占便宜。实测把它们都算进来比只算 `lens_app` 与 `lens_llm` 慢约 2 s，属于噪声范围，因此保留了 “链接 Qt 就加” 这条更简单的规则：核数更少的机器（CI）上 CPU 总量更重要，简化规则更稳。

QML 提前编译那 152 s 降到 27 s，是预编译头贡献的主体。

3. **测试全部退出默认构建**：六个测试目标都带 `EXCLUDE_FROM_ALL`，`all` 从 140 个边缘降到 120，只剩主程序 `lens`。理由是它们跟着 `src/` 每次改动重编没有意义：只有 QML 或 `lens_app` / `lens_llm` 的接口变了才会失效。按名构建照旧（`./scripts/build.bat --target lens_gtest_unit`），CI 的构建步骤本来就逐个列名，不受影响；`scripts/coverage.sh` 遇到没构建的目标会跳过并提示。

4. **另加一棵优化的构建树**，`scripts/build-release.bat` → `build-ninja-release`（RelWithDebInfo）。需要跑得快、看真实性能时用这棵，开发树保持 Debug。这是第二棵树而不是就地切换构建类型：换类型会重写全部编译选项，等于整树重编一次。选 RelWithDebInfo 而不是 Release，是因为 MSVC 的 Release 选项是 `/O2 /Ob2 /DNDEBUG`，不带 `/Zi`：没有 PDB，崩溃调用栈和 `scripts/coverage.sh` 就都没得读。

   附带的实测：Release 并不省构建时间。同一个 `--target lens`，Debug 墙钟 34.9 s、CPU 109 s；Release 墙钟 34.8 s、CPU 132 s。优化本身要花 CPU，Release 换来的是运行速度，不是编译速度。

## 没有采用的方案

- **`/Z7`（把调试信息写进 `.obj`，绕开 MSVC 共享 PDB 的写入串行）**：怀疑过它，实测没有差别（47 s 对 48 s）。第一次测量的 113 s 是新建构建目录后文件系统冷缓存与实时扫描造成的，换一个目录、系统缓存已热，同样的全量编译是 46 s。同一台机器上第一次编译一个新目录要额外付约一分钟，与本次改动无关。
- **`CMAKE_UNITY_BUILD`**：全量编译 36 s，比预编译头更快，但它把若干源文件并成一个 TU，改一行要重编一整批，日常增量编译会变慢，编译错误的报错位置也会变。日常主要用增量编译，收益与代价不合算。

## 验证

改后的构建树里：`lens_gtest_unit` 29 项全过；`lens_qtest_components` 43 项通过、1 项跳过；`lens_qtest_surfaces` 24 项通过。单个源文件改动后的增量编译约 5 s。

默认构建不再产出三个非门禁套件的可执行文件；按名构建的路径实测可用：`--target lens_gtest_smoke` 10 s，`--target lens_gtest_perf lens_gtest_integration` 12 s。CI 步骤里那一串 `--target`（含 `lens_gtest_perf`）在干净树上跑通，五个可执行文件都在。

所有数字都取自本机（16 核、32 GB、Ninja 1.12.1、MSVC 19.44、Qt 6.9.0、Debug，系统缓存已热），用 `ninja` 自己写的 `.ninja_log` 统计，不经过任何计时包装。
