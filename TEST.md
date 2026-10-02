# 测试

> 本文是测试的唯一出处：框架、目录、目标、运行方式、样例集格式、profiling 与记录约定。
> `CLAUDE.md` 的 Test 与 Profiling 两节只留指针；`PHASE1.md` §4.5 与 §4.6 只留契约要点。
> 设计总览见 `DESIGN.md`，模块接口见 `PHASE1.md`。

## 1 框架与目录

框架是 GoogleTest，vendor 在 `third_party/googletest`（v1.18.0），照 `third_party/spdlog` 的做法：
`add_subdirectory` 之后只用它导出的 `GTest::gtest` / `GTest::gtest_main`，`BUILD_GMOCK` 与
`INSTALL_GTEST` 关掉（前者没用到，后者会往项目里塞安装规则）。

```
test/
├── eval_corpus.json      # 自检样例集，兼行为规格（§3）
├── records/              # 运行记录，gitignored（§5）
└── googletest/
    ├── support.h         # 仓库根 + 词表与屈折表的加载（无 Qt）
    ├── llm_support.h     # 线上协议加载（含 Qt，只有 LLM 侧目标包含）
    ├── unit/             # 离线，必绿
    ├── integration/      # 真机 Windows API，人工执行
    ├── perf/             # 只测量，不对时间下断言
    └── smoke/            # 真模型，人工执行
```

目录名按用例性质分组：`unit` / `integration` / `e2e` / `perf` / `smoke`。当前 `unit`、
`integration`、`perf`、`smoke` 有内容；`e2e` 等第一个用例出现时再建目录——git 不跟踪空目录。
Qt 与 QML 侧的测试将来用 QTest 另立 `test/qtest/`，与这里平行，不混在一起。

## 2 目标与运行

| 目标 | 内容 | 何时跑 |
| --- | --- | --- |
| `lens_gtest_unit` | 样例集 / KnownStore 往返 / LLM 纯函数 | 每次改动，可进 CI |
| `lens_gtest_integration` | 选区捕获：手势规则 / 终端排除 / 钩子与剪贴板的真机往返 | 动选区入口时，人工执行 |
| `lens_gtest_perf` | FilterCore 吞吐 + profiling 报告 | 动内核时 |
| `lens_gtest_smoke` | 1 词真模型往返 | 动 LLM 链路时，人工执行 |

```
PATH=/b/qtt/6.9.0/msvc2022_64/bin:$PATH QT_FORCE_STDERR_LOGGING=1 \
  ./build-ninja/test/googletest/lens_gtest_unit.exe
```

- 链接 `lens_llm` 或 `lens_app` 的目标需要 Qt DLL 在 `PATH` 上（即 `unit` / `integration` /
  `smoke`）。`lens_gtest_perf` 只链 `lens_core`，保持无 Qt，什么都不需要。
- 选区捕获：`lens_gtest_integration`。默认 11 例绿、1 例 skip——跳过的那条要人手拖鼠标。
  `LENS_HOOK_SMOKE=1` 放开它，`LENS_HOOK_SMOKE_TEXT` 指定要拖选的词（缺省 `ubiquitous`）。它先往剪贴板放一个哨兵串，再等人在**别的窗口**里拖选那个词，然后断言三件事：取到的文本相符、返回状态是
  `Captured`、哨兵串还在剪贴板上（这是唯一能自动验证 “不丢读者剪贴板” 的地方）。提权窗口（任务管理器、管理员控制台）会被 UIPI 挡下注入，表现为超时而不是报错，别拿它试；终端类进程按名字排除，是故意的。
- 那两例真机用例会合成鼠标事件，指针会被移动并复位；拖拽落点是一个测试自己创建的顶层小窗口，不会点到读者的界面。
- 不接 ctest：`gtest_discover_tests` 会在构建期执行测试程序，等于要求构建环境也把 Qt DLL 摆在
  `PATH` 上，不值得这层耦合。
- 冒烟：`LENS_SMOKE_WORD=ubiquitous ./build-ninja/test/googletest/lens_gtest_smoke.exe`。词取自
  `LENS_SMOKE_WORD`，缺省 `ubiquitous`。`settings.local.json` 缺 URL / MODEL / API-KEY 时报 skip
  而不是失败——要 key、要花钱、结论靠人看，所以它不进 CI。密钥读进内存后不打印、不进日志、不进
  失败消息。
- `LENS_LOG_LEVEL=trace` 提高日志级别。

## 3 样例集

`test/eval_corpus.json` 是**行为规格**。每段一项：`text`（必填）、`expect`（必填，surface 序列）、
`expectLemmas`（选填）、`minFreqRank`（选填，缺省 0）、`known`（选填）、`note`（选填）。

改 FilterCore 的行为先改样例集，`lens_gtest_unit` 红了再动 `src/`。样例集撞出误判就在那里加一段，
不要先把断言放宽。

## 4 Profiling

`src/core/profile.{h,cpp}` 放 `lens_core` 内部的测量点，两个原语：`LENS_PROFILE_SCOPE` 是 RAII
作用域计时器，`LENS_PROFILE_COUNT` 是计数器。二者在 `LENS_PROFILE` 未定义时展开为空，测量代码一字
不改。

开关是 CMake 选项 `LENS_ENABLE_PROFILE`，挂在 `lens_core` 的 **PUBLIC** 上：调用点要不要发样本由
这个宏决定，写入侧与读取侧必须看到同一个值。**开它等于重编 `lens_core`，不是重跑。**

profile 数字要有优化才有意义，而两个开发 preset 都是 Debug，所以用第三个树：

```
cmake --preset ninja-qt6-perf
cmake --build --preset ninja-qt6-perf --target lens_gtest_perf
./build-ninja-perf/test/googletest/lens_gtest_perf.exe
```

（与 `ninja-qt6` 一样需要 MSVC 环境，见 `CLAUDE.md` 的 Build 一节。）

三条必须守住：

- **不逐条打日志**。热路径按调用记日志会淹掉日志本身，并盖住要观察的现象。站点只累加，热阶段结束
  后用一次 `report()` 出汇总表；`profile.h` 自身不写任何日志。
- **计时器不免费**。同株关掉插桩作对照，插桩整体抬高约 16%，反推一次 `ScopeTimer` 约 60 ns（两次
  `steady_clock` 读 + 一次注册表查找）。对 `lemmatize` 这种每 token 进入的作用域不可忽略：先用计数器
  量分母，再决定要不要给内层计时。
- **一次结论至少三轮**。本机噪声大：同一次运行的 min 与 max 可差 4 倍，`filterWords` 在五轮里从
  4.36 ms 摆到 7.04 ms，而 `lemmatize` 稳定在 1.85–2.04 ms。报占比要报区间，不报点值。

注册表以字面量的地址为键，所以每次样本不必哈希或拷贝字符串；单线程无锁（`profile.cpp` 的
`ponytail:` 注释写了升级路径）。`report()` 是 “进程启动至今” 的累计，`lens_gtest_perf` 在计时轮次
前调 `reset()`，使报告只覆盖被测量的那一段。

## 5 记录

`test/records/` 存运行快照，**gitignored**——那是本机的，不进仓库。一次值得留档的运行写一个 HTML，
文件名是日期加主题（如 `2026-10-02-full-run.html`）。

页面形态：白底、系统字体、单列窄栏、无装饰。表格只有横向细分隔线，数字用等宽字体，段落不缩进。CSS
内联，不引任何外部资源，离线可看，打印可读。

转写规则：数字照抄原始输出，不重算、不四舍五入、不补齐；一次结论至少三轮（§4）；对不上就重跑，
不要修数字。

记录目前是手工从运行输出转写的。等它变成例行动作再写生成器——在那之前，生成器只是多一个会腐烂的
中间层。
