# 测试

> 本文是测试的唯一出处：框架、目录、目标、运行方式、样例集格式、profiling、覆盖率与记录约定。
> `AGENTS.md` 的 Test 与 Profiling 两节只留指针；`PHASE1.md` §4.5 与 §4.6 只留契约要点。
> 设计总览见 `DESIGN.md`，模块接口见 `PHASE1.md`。

## 1 框架与目录

框架是 GoogleTest，vendor 在 `third_party/googletest`（v1.18.0），照 `third_party/spdlog` 的做法：
`add_subdirectory` 之后只用它导出的 `GTest::gtest` / `GTest::gtest_main`，`BUILD_GMOCK` 与
`INSTALL_GTEST` 关掉（前者没用到，后者会往项目里塞安装规则）。

```
test/
├── eval_corpus.json      # 自检样例集，兼行为规格（§3）
├── records/              # 运行记录，gitignored（§5）
├── googletest/           # 不需要窗口的那一半
│   ├── support.h         # 仓库根 + 词表与屈折表的加载（无 Qt）
│   ├── llm_support.h     # 线上协议加载（含 Qt，只有 LLM 侧目标包含）
│   ├── unit/             # 离线，必绿
│   ├── integration/      # 真机 Windows API，人工执行
│   ├── perf/             # 只测量，不对时间下断言
│   └── smoke/            # 真模型，人工执行
└── qtest/                # 要一个窗口、要跑 QML 的那一半
    ├── main.cpp          # 每个 QTest 目标共用的 runner
    ├── setup.{h,cpp}     # 表面目标要的 Controller / Tray，见 §2
    ├── testutil.js       # 两个目录共用的走树助手
    ├── components/       # 组件级用例（tst_*.qml）
    └── surfaces/         # 表面级用例（tst_*.qml）加 settings.json
```

`googletest/` 的目录名按用例性质分组：`unit` / `integration` / `e2e` / `perf` / `smoke`。当前
`unit`、`integration`、`perf`、`smoke` 有内容；`e2e` 等第一个用例出现时再建目录——git 不跟踪空
目录。QML 与 Qt 侧的测试用 QTest，另立在 `test/qtest/`，与那里平行，不混在一起：它们要一个
`QGuiApplication` 和一个 Qt Quick 场景，gtest 两样都给不了。

## 2 目标与运行

| 目标 | 内容 | 何时跑 |
| --- | --- | --- |
| `lens_gtest_unit` | 样例集 / KnownStore 往返 / LLM 纯函数 | 每次改动，可进 CI |
| `lens_gtest_integration` | 选区捕获：手势规则 / 终端排除 / 钩子与剪贴板的真机往返 | 动选区入口时，人工执行 |
| `lens_gtest_perf` | FilterCore 吞吐 + profiling 报告 | 动内核时 |
| `lens_gtest_smoke` | 1 词真模型往返 | 动 LLM 链路时，人工执行 |
| `lens_qtest_components` | `qml/components/` 那八个的接线与交互 | 动组件时，无头，可进 CI |
| `lens_qtest_surfaces` | 表面：Main 的摆放与关闭、托盘菜单、三张面板、行动条 | 动表面时，无头，可进 CI |

```
PATH=/b/qtt/6.9.0/msvc2022_64/bin:$PATH QT_FORCE_STDERR_LOGGING=1 \
  ./build-ninja/test/googletest/lens_gtest_unit.exe

PATH=/b/qtt/6.9.0/msvc2022_64/bin:$PATH QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 \
  ./build-ninja/test/qtest/lens_qtest_components.exe

PATH=/b/qtt/6.9.0/msvc2022_64/bin:$PATH QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 \
  ./build-ninja/test/qtest/lens_qtest_surfaces.exe
```

- 链接 `lens_llm` 或 `lens_app` 的目标需要 Qt DLL 在 `PATH` 上（即 `unit` / `integration` /
  `smoke`）。`lens_gtest_perf` 只链 `lens_core`，保持无 Qt，什么都不需要。
- 选区捕获：`lens_gtest_integration`。默认 14 例绿、1 例 skip——跳过的那条要人手拖鼠标。
  `LENS_HOOK_SMOKE=1` 放开它，`LENS_HOOK_SMOKE_TEXT` 指定要拖选的词（缺省 `ubiquitous`）。它先往剪贴板放一个哨兵串，再等人在**别的窗口**里拖选那个词，然后断言三件事：取到的文本相符、返回状态是
  `Captured`、哨兵串还在剪贴板上。剪贴板存还原本身另有 `ClipboardSnapshot.*` 三例自动覆盖，不依赖这一条——把它交给人工的那段时间里，它正是坏的。那三例直接操真实的剪贴板，而剪贴板是全机共享的：约 35 次运行里见过 3 次失败，都在别的进程刚写过剪贴板之后，按需复现不了，加重试也没挡住。所以那里单次失败先重跑一次再当信号看，成因仍未知，如实记着。提权窗口（任务管理器、管理员控制台）会被 UIPI 挡下注入，表现为超时而不是报错，别拿它试；终端类进程按名字排除，是故意的。
- 那两例真机用例会合成鼠标事件，指针会被移动并复位；拖拽落点是一个测试自己创建的顶层小窗口，不会点到读者的界面。
- 不接 ctest：`gtest_discover_tests` 会在构建期执行测试程序，等于要求构建环境也把 Qt DLL 摆在
  `PATH` 上，不值得这层耦合。QTest 侧同理，同样不接。
- QTest 的用例是 `.qml`，从源码树直接读，不进任何模块的 `QML_FILES`，所以 qmllint 不看它们，那
  条棘轮不受影响。它们要 `import Lens` 才跑得起来，因此目标链 `lens_app` 与 `lens_appplugin`：
  静态插件只有它的 init 对象被链进来才导入，少了这一条，组件会以 “不是类型” 而不是以断言失败
  的形式消失。
- 用例文件的根是一个 `Item`，`TestCase` 是它的孩子而不是根。`TestCase` 自带 `visible: false`，
  挂在它下面的东西于是收不到鼠标事件——而鼠标事件正是这个目标替掉人眼的那一半。
- 图标资源挂在 `lens_app` 上，不在可执行文件里：`:/icons/` 只在带着它的二进制里解析，而读图标的是表面。
  挂错地方时两个 QTest 目标每个用例刷一条 `QML Image: Cannot open`，一共三百多条。
- 剩下的是 Qt 自己发的两类：offscreen 平台不能抬窗口，而表面显示时 `raise()` 是必需的；Qt 6 不再随包带字体，
  `setup.cpp` 自己按文件载入。它们都走不带类别的 `qWarning()`，按类别过滤会连带盖掉其余无类别警告，所以留着，
  一次运行合计十条左右。
- 两个目标的差别只有一处：`lens_qtest_surfaces` 编译 `setup.cpp` 时定义 `LENS_QTEST_SINGLETONS`，
  组件目标不定义。`Controller` / `Tray` 的工厂在没人 `provide()` 时断言、Debug 下直接崩进程，而
  引擎自己造不出来（构造函数要 store、client 与 hook），所以 setup 照 `main()` 的做法造好再交出
  去——只是**不装鼠标钩子、不显示托盘图标**。组件用例碰不到这两样，也就不必背一份配置。
- 表面用例跑的是签入的 `surfaces/settings.json`，不是 `settings.local.json`，两个理由：断言要在每
  台机器上看到同一组值，而设置面板的 setter 会落盘——指向仓库里那份，一条切换主题的用例就会改掉一
  个被跟踪的文件。setup 把夹具拷进临时目录再加载，进程退出时删掉。
- 夹具里 `selectionCapture` 写的是字符串 `"false"`，不是 JSON 布尔。`writeDocument` 写的是字符
  串，`documentString` 也只认字符串，写成 `false` 会被当作缺省值 `"true"` 静默忽略——这是手改这份
  文档时最先踩到的一格。
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

## 4 Profiling 与覆盖率

### Profiling

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

（与 `ninja-qt6` 一样需要 MSVC 环境，见 `AGENTS.md` 的 Build 一节。）

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

### 覆盖率

两条线，两条完全不同的技术路线，**读数不可比，也不许平均**。哪个数字对应哪条线，看目标的语言。

#### C++ 行覆盖

工具是 OpenCppCoverage，winget 装在 `C:\Program Files\OpenCppCoverage\`。它读 Debug 二进制的
PDB、在运行时插桩，所以**没有覆盖率专用树，也没有覆盖率编译标志**：量的是 `build-ninja` 里已有
的那批产物。这一点与上面那棵树相反——profiling 要优化过，覆盖率不要。

```
sh scripts/coverage.sh                                    # 五个无人值守的目标
sh scripts/coverage.sh test/records/coverage lens_gtest_unit   # 或者只点几个目标
```

每个目标一份，最后合并成 `test/records/coverage/coverage.xml`；该目录 gitignored。汇总那一行由
`scripts/coverage-summary.py` 算出：Cobertura 里一个被多个二进制链进去的源文件会出现好几份，
根节点的 `line-rate` 把它们加在一起，直接读会偏低。`lens_gtest_smoke` 要 key 且花钱，不在内；
`lens_gtest_integration` 在默认列表里，但 CI 不点它（runner 没有真鼠标与真剪贴板）。

两条口径，读数字之前先读它：

- **没报出来的文件不是 0%**。只被 `lens.exe` 引用的 `main.cpp` 与 `qt_log.cpp` 不会被静态链接拉进
  测试二进制，声明与宏居多的头文件也没有可执行行——两种都不等于 “没被测过”。
- **`profile.cpp` 的读数偏低是构建配置**。`LENS_ENABLE_PROFILE` 在这棵树上关着，测量点在调用点
  展开成空。

#### QML 执行覆盖

Qt 没有 QML 的行覆盖率工具，这不是遗漏而是结构问题：`.qml` 主要在声明对象，而声明一个对象不是一
行 “执行”。会执行的是它里面的 JavaScript——属性绑定、signal handler、函数——唯一报告这些的是
QML profiler：每求值一次就记一条带文件与行号的事件，这些位置的并集就是执行集。

```
npm ci                    # tree-sitter，与 AST 度量共用同一个依赖
sh scripts/qml-coverage.sh
```

分母来自 `scripts/qml-coverage.js` 用 `tree-sitter-qmljs` 建的解析树：模块里每个非字面量的
`ui_property` 与 `ui_binding`，加上每个 `function_declaration`。字面量的绑定被排除在两侧之外
——编译器把它们折进对象的构造，引擎从不求值，profiler 也就永远报不出来；算进分母就是一笔永远
扣不掉的分。结果写 `test/records/qmlcov/`。profiler 启动的是那**两个 QTest 目标**本身，所以这是
测试套件覆盖到多少，不是手工跑一遍应用覆盖到多少。

三条要注意的：

- **它数位置，不数行。**一个绑定只要对象被创建就会求值一次，所以 “两条分支都测过没有” 它答不上
  来。这不是行覆盖率的替代品。
- **无头不渲染，`qsTr()` 标签绑定永不被求值。**未执行的位置里约四分之一是这一条造成的，那不是
  缺口。
- **构建树必须是新的。**QML 编进二进制，改了 `.qml` 没重新链接测试目标就量到旧模块——本轮真踩过
  一次，同一个文件读 19% 与 52%。同理适用于 C++ 那条线。

数字读出来之后怎么写、往哪写，见 `docs/metrics/` 里按日期存的那份报告；补什么、按什么顺序补，
见 `docs/metrics/coverage/coverage-plan-2026-10-03.md`。

## 5 UI 表面

表面没有主窗口，全部靠触发才出现，所以真机验证只能靠驱动与拍照。能脱离屏幕断言的那部分已经搬进
`lens_qtest_surfaces`（§2）：摆放、关闭规则、面板上的数字、托盘菜单那几个派生值，都不再需要快门。
剩下的仍是这里的事——卡片对没对齐、阴影糊不糊、图标画出来没有。`scripts/ui-*.ps1` 是三个这样的工具，
都是 PowerShell，都先把自己设成 per-monitor-v2 感知——不设的话截到的是 Windows 已经拉伸过的位图，
糊与偏移都会被量成假的。

- `ui-capture.ps1`：截屏。`-Virtual` 截整个虚拟桌面，`-Crop x,y,w,h` 裁一块，`-Zoom` 最近邻放大，
  `-Profile` 打出穿过中间那一行的灰度值。清晰还是糊、卡片对没对齐，靠这行数字定案，光看图看不出来。
  虚拟桌面那一档不是可选项：表面不只落在主屏上，而 `GetSystemMetrics(0/1)` 只知道主屏。
- `ui-input.ps1`：驱动指针，三个脚本里只有它会动指针。`click` 点一下，`drag` 等步进拖，`dragfast`
  相对位移连发，`reveal` 把指针停到主屏最后一行、把自动隐藏的任务栏勾出来。低层鼠标钩子看得见注入
  事件，所以拖选、点外部、拖面板都能这样触发。等步进那一档看着没问题，密度超过手速的 `dragfast`
  才拖出过发散——两者都要跑。
- `ui-tray-rects.ps1`：打任务栏与通知区的窗口矩形，是 QML 侧位置的比对基准。托盘图标报的坐标是
  设备无关像素、自动隐藏的任务栏其矩形落在主屏下方，两件事都是拿它查出来的。

另有一个不驱动也不拍照的：`mouse-stall-probe.ps1` 在应用启动的那几秒里连发 `mouse_event` 并给每次调用
计时，报告慢过阈值的调用。它回答的是 “鼠标被抢走了吗”，不是 “界面画对了吗”——回归对象是钩子跑在自己线程
上那条约束（`docs/QML.md` §5），一次运行三四十秒。

还有一条既不驱动、也不显示窗口的路：`qml-snapshot.ps1` 用 Qt 的 `offscreen` 平台跑 QTest，把真实 QML 场景
渲进内存图再写成 PNG。它不显示窗口、不动鼠标、不抓桌面，所以不受桌面遮挡影响，也不需要人工在屏幕前。方法
分两处：`setup.cpp` 用 `QFontDatabase::addApplicationFont` 逐个载入 Windows 字体文件，再把输出目录经
`lensQaSnapshotDir` 交给每个引擎——offscreen 平台本身不带字体，不载就是满屏方框；用例调用 `testutil.js` 的
`saveSnapshot(testCase, item, name)`，内部走 `TestCase.grabImage(item)` 再 `save()`。脚本设好
`QT_QPA_PLATFORM=offscreen` 与 `LENS_QA_SNAPSHOT_DIR` 后驱动目标，默认写到 `test/records/snapshots/`。它验的
是渲染结果本身——字形、字重、对齐、配色；摆放与交互仍归 §2 的 `lens_qtest_surfaces` 和上面那三件套。

一轮的顺序：先 `ui-tray-rects.ps1` 量基准，再起应用，用 `ui-input.ps1` 驱动，用 `ui-capture.ps1`
截图，必要时 `-Profile` 或放大看像素。为看清而临时加进 QML 的东西（计时器、`console.log`）**提交前
必须删干净**——`.githooks/pre-commit` 的第四项会拦下 `src/` 下带这两个串的暂存文件，手工确认是
`grep -rn PROBE src/`。观察点本身由应用自己的日志承担：`logs/lens.log` 每行都刷，可以 `tail -f`。

驱动时有两个坑，都是撞了六七次调用才摸出来的：

- **焦点与拖拽必须落在同一个 PowerShell 进程里**。拆成两次调用，中间那次的控制台会抢走前台焦点，注入的
  Ctrl+C 于是送去了控制台而不是被拖选的应用——取文拿到的是控制台里的东西，或者什么都没有。
- **取词只认发生变化的剪贴板**。重选同一段文本之前要先用 `Set-Clipboard` 换掉剪贴板内容，否则钩子读回来
  的还是上一次的字符串，日志只会说 `did not change within 250 ms`，看着像钩子没触发。

## 6 记录

`test/records/` 存运行快照，**gitignored**——那是本机的，不进仓库。一次值得留档的运行写一个 HTML，
文件名是日期加主题（如 `2026-10-02-full-run.html`）。

页面形态：白底、系统字体、单列窄栏、无装饰。表格只有横向细分隔线，数字用等宽字体，段落不缩进。CSS
内联，不引任何外部资源，离线可看，打印可读。

转写规则：数字照抄原始输出，不重算、不四舍五入、不补齐；一次结论至少三轮（§4）；对不上就重跑，
不要修数字。

记录目前是手工从运行输出转写的。等它变成例行动作再写生成器——在那之前，生成器只是多一个会腐烂的
中间层。
