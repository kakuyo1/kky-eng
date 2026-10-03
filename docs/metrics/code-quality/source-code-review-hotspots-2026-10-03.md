# 源码审阅热点报告

## 1、报告信息

| 项目 | 内容 |
| --- | --- |
| 分析范围 | `src/` 下的 C++、C++ 头文件和 QML 文件 |
| 生成日期 | 2026-10-03 |
| C++ 分析工具 | `lizard 1.24.0` |
| 分析方式 | C++ 使用函数级静态度量，QML 使用模块清单、文件级结构统计和 `qmllint --json` |
| 第三方代码 | 未纳入 |
| 构建产物 | 未纳入 |

本报告用于定位需要优先审阅的代码热点，不把单个指标当作代码正确性或架构质量的充分证明。

## 2、执行摘要

`src/` 共包含 47 个源码文件，物理行数为 7320 行。其中 C++ 文件 29 个，QML 文件 18 个。

C++ 共分析出 155 个函数，文件级 NLOC 为 2738 行，平均圈复杂度为 3.61。整体平均值处于可维护范围，但复杂度集中在少数函数：

- 8 个函数的圈复杂度大于 10。
- 3 个函数的圈复杂度大于 20。
- 10 个函数的物理函数长度大于 50 行。
- 最高圈复杂度为 38，位于 `core::lemmatize`。
- 没有函数超过 5 个参数。

优先级最高的热点是 `lemmatize`、`parseExplanations`、`filterWords` 和 `KnownStore::load`。它们分别把词形归一化、模型响应校验、文本筛选和持久化加载的多个步骤集中在单个函数中。

## 3、范围和规模

### 3.1 文件和物理行数

| 范围 | 文件数 | 物理行数 |
| --- | ---: | ---: |
| `src/core` C++ 和头文件 | 10 | 1399 |
| `src/llm` C++ 和头文件 | 10 | 1001 |
| `src/app` C++ 和头文件 | 9 | 2228 |
| C++ 合计 | 29 | 4628 |
| `src/app/qml` QML | 18 | 2692 |
| `src` 合计 | 47 | 7320 |

### 3.2 C++ 模块分布

| 模块 | 函数数 | NLOC | 圈复杂度合计 | 平均圈复杂度 |
| --- | ---: | ---: | ---: | ---: |
| `app` | 78 | 1320 | 234 | 3.00 |
| `core` | 51 | 822 | 227 | 4.45 |
| `llm` | 26 | 596 | 99 | 3.81 |

`core` 的平均圈复杂度最高，主要由词形处理和持久化读取函数贡献；`app` 的函数和 NLOC 数量最多，主要集中在启动编排、选择抓取和 QML 控制器接口。

## 4、C++ 指标

### 4.1 函数级阈值统计

| 指标 | 结果 | 本次采用的警戒线 |
| --- | ---: | ---: |
| 函数总数 | 155 | - |
| 平均圈复杂度 | 3.61 | 10 |
| 圈复杂度大于 10 | 8 | 10 |
| 圈复杂度大于 20 | 3 | 20 |
| 函数长度大于 50 行 | 10 | 50 |
| NLOC 大于 30 | 13 | 30 |
| 参数数大于 3 | 5 | 3 |
| 参数数大于 5 | 0 | 5 |
| 最大嵌套结构为 4 层 | 2 | 3 |

阈值用于排序和复查，不是对所有超阈值函数自动判定为缺陷。初始化器、平台适配代码和错误处理分支可能合理地增加函数长度，但仍应检查是否存在可独立命名的职责。

### 4.2 最高优先级热点

| 函数 | 位置 | NLOC | CCN | 函数长度 | 原始 MI |
| --- | --- | ---: | ---: | ---: | ---: |
| `lens::core::lemmatize` | [`filter_core.cpp:193`](../../../src/core/filter_core.cpp#L193) | 76 | 38 | 107 | 47.3 |
| `lens::llm::parseExplanations` | [`llm_pure.cpp:82`](../../../src/llm/llm_pure.cpp#L82) | 80 | 18 | 102 | 51.9 |
| `lens::core::filterWords` | [`filter_core.cpp:301`](../../../src/core/filter_core.cpp#L301) | 58 | 27 | 75 | 57.6 |
| `lens::core::KnownStore::load` | [`known_store.cpp:23`](../../../src/core/known_store.cpp#L23) | 54 | 21 | 64 | 58.4 |
| `main` | [`main.cpp:112`](../../../src/app/main.cpp#L112) | 60 | 10 | 110 | 59.2 |
| `lens::llm::Pricing::load` | [`llm_pricing.cpp:14`](../../../src/llm/llm_pricing.cpp#L14) | 49 | 14 | 60 | 62.2 |
| `lens::llm::loadLlmProtocol` | [`llm_protocol.cpp:86`](../../../src/llm/llm_protocol.cpp#L86) | 48 | 9 | 55 | 63.4 |
| `lens::app::SelectionTextGrabber::grab` | [`selection_text_grabber.cpp:330`](../../../src/app/selection_text_grabber.cpp#L330) | 54 | 12 | 74 | 64.2 |
| `lens::core::loadIrregulars` | [`filter_core.cpp:140`](../../../src/core/filter_core.cpp#L140) | 45 | 14 | 52 | 64.3 |

原始 MI 使用本项目度量指南中的公式计算：

```text
MI = 171 - 5.2 × ln（Halstead Volume） - 0.23 × CCN - 16.2 × ln（NLOC）
```

本报告中的 Halstead Volume 来自 `lizard` 的 `-Ehalstead` 扩展。MI 不是 `lizard` 的原生输出，且不同工具对注释、规模和归一化方式的处理不同，因此只用于同一份报告内的相对排序。

### 4.3 热点解释

#### `core::lemmatize`

代码位置：[`src/core/filter_core.cpp:193`](../../../src/core/filter_core.cpp#L193)。

该函数依次处理前置条件、不规则词、多个后缀规则、候选词生成和频率排名选择。CCN 38 是本次分析的最高值，建议优先拆分为以下内部步骤：

- 不规则词候选选择。
- 后缀规则候选生成。
- 候选词频率比较。

拆分时应保持当前规则顺序和频率优先级不变，并用现有词形测试覆盖每条规则。

#### `llm::parseExplanations`

代码位置：[`src/llm/llm_pure.cpp:82`](../../../src/llm/llm_pure.cpp#L82)。

该函数同时负责响应 envelope 检查、内容 JSON 检查、结果字段检查、空字段检查、重复词检查和请求顺序恢复。建议将协议层检查、单条结果转换和结果排序分开，使每类失败原因可以独立测试。

#### `core::filterWords`

代码位置：[`src/core/filter_core.cpp:301`](../../../src/core/filter_core.cpp#L301)。

该函数同时执行空白分词、首尾清洗、粘连字符过滤、长度过滤、大小写过滤、元音过滤、词形归一化、词表过滤、已知词过滤和去重。建议按数据流阶段提取步骤，但不要改变当前短路顺序，因为每个过滤条件都会影响后续的处理成本和结果。

#### `core::KnownStore::load`

代码位置：[`src/core/known_store.cpp:23`](../../../src/core/known_store.cpp#L23)。

该函数包含文件读取、JSON 顶层校验、等级和语言读取、known 标记读取以及 cache 读取。可以把 `known` 和 `cache` 的解析提取为私有加载步骤，同时保留当前 `文件存在但 JSON 损坏时不重置` 的行为。

#### `main`

代码位置：[`src/app/main.cpp:113`](../../../src/app/main.cpp#L113)。

`main` 的 CCN 为 10，未达到高复杂度区间，但函数长度达到 106 行。它集中编排日志、数据文件、配置、翻译、QML context property、托盘和鼠标 hook。若启动流程继续增长，建议提取启动阶段函数；当前不必仅为降低指标而重构。

## 5、QML 指标

QML 使用 [`scripts/qml-metrics.ps1`](../../../scripts/qml-metrics.ps1) 读取 CMake 生成的 QML response file（当前为 `lens_app.rsp`），只统计正式 `Lens` 模块中的文件。脚本同时解析 `qmllint --json`，因此不会把 response file 之外的临时 QML 文件纳入结果。

现有 PowerShell 脚本中的对象、绑定和词法大括号深度是源码表面统计，不等同于 QML AST 指标。近似结构分数定义为：

```text
1 + if/for/while 数量 + &&/|| 数量
```

该分数不等同于 QML 的标准圈复杂度，不能与 C++ 的 CCN 直接比较。

### 5.1 文件规模

QML module 的 18 个文件由 9 个顶层 surface 和 9 个 `components` 文件组成。两组都来自同一个 QML response file，并共同参与下方的总计。

| 文件组 | 文件数 | 物理行 | 代码行 | QML 对象 | 方法 | signal handler | 属性绑定 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 顶层 surface | 9 | 2010 | 1504 | 204 | 27 | 47 | 756 |
| `components` | 9 | 682 | 453 | 58 | 3 | 9 | 218 |
| 合计 | 18 | 2692 | 1957 | 262 | 30 | 56 | 978 |

以下列出规模和结构分数最高的 5 个顶层 surface；`components` 的完整明细见后表。

| 文件 | 物理行 | 非空、非注释行 | 近似结构分数 |
| --- | ---: | ---: | ---: |
| [`SettingsPopup.qml`](../../../src/app/qml/SettingsPopup.qml) | 328 | 276 | 4 |
| [`Bubble.qml`](../../../src/app/qml/Bubble.qml) | 362 | 268 | 7 |
| [`Main.qml`](../../../src/app/qml/Main.qml) | 305 | 188 | 30 |
| [`TrayMenu.qml`](../../../src/app/qml/TrayMenu.qml) | 242 | 188 | 3 |
| [`WordsPopup.qml`](../../../src/app/qml/WordsPopup.qml) | 226 | 167 | 6 |

### 5.1.1 `components` 明细

| 文件 | 物理行 | 代码行 | QML 对象 | 方法 | signal handler | 属性绑定 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| [`Tokens.qml`](../../../src/app/qml/components/Tokens.qml) | 62 | 30 | 1 | 0 | 0 | 1 |
| [`Icon.qml`](../../../src/app/qml/components/Icon.qml) | 40 | 24 | 3 | 0 | 0 | 12 |
| [`ShadowCard.qml`](../../../src/app/qml/components/ShadowCard.qml) | 128 | 70 | 7 | 1 | 2 | 33 |
| [`Segment.qml`](../../../src/app/qml/components/Segment.qml) | 59 | 41 | 7 | 0 | 1 | 21 |
| [`Switch.qml`](../../../src/app/qml/components/Switch.qml) | 50 | 35 | 5 | 0 | 1 | 17 |
| [`SwitchRow.qml`](../../../src/app/qml/components/SwitchRow.qml) | 41 | 30 | 4 | 0 | 1 | 17 |
| [`StatRow.qml`](../../../src/app/qml/components/StatRow.qml) | 66 | 49 | 7 | 0 | 1 | 32 |
| [`DropdownField.qml`](../../../src/app/qml/components/DropdownField.qml) | 171 | 126 | 16 | 2 | 2 | 59 |
| [`MenuRow.qml`](../../../src/app/qml/components/MenuRow.qml) | 65 | 48 | 8 | 0 | 1 | 26 |

QML 合计为 2692 行物理行，其中非空、非注释行约 1957 行，注释行 407 行。脚本另外统计到 262 个 QML 对象、30 个 QML 方法、56 个 signal handler、41 个属性声明、12 个 signal 声明、978 个属性绑定、35 个条件表达式、16 个逻辑运算符，最大词法大括号深度为 9。还识别到 1 个 `Loader`、1 个 `ListView`、4 个 `Repeater`、2 个 `Connections`、7 个 `Timer`、5 个 `Behavior` 和 4 个 effect。上述总计包含 `components` 文件。

这些对象、绑定和词法大括号指标用于同一项目内的趋势比较，不应解释为标准 QML 圈复杂度。`Main.qml` 的条件和逻辑表达式合计最高，其中 8 个条件来自 profiler 自动化用的 `profileScenario`；排除这个 debug-only 入口后，产品逻辑仍主要集中在多屏幕定位、点击外部关闭和多个 `Connections` 处理器。

### 5.2 AST 结构和 JavaScript 复杂度

`scripts/qml-ast-metrics.js` 使用 `tree-sitter-qmljs 0.3.1` 对同一个 QML response file 清单进行 AST 分析。当前 18 个文件全部解析成功，没有 `ERROR` 或 `MISSING` 节点。

| 指标 | 顶层 surface | `components` | 合计 |
| --- | ---: | ---: | ---: |
| 文件数 | 9 | 9 | 18 |
| AST QML 对象 | 210 | 62 | 272 |
| AST 属性绑定 | 696 | 195 | 891 |
| signal handler | 47 | 9 | 56 |
| 绑定成员根引用 | 103 | 44 | 147 |
| 绑定边 | 429 | 161 | 590 |

全模块 AST 指标：最大对象树深度为 8，命名 JavaScript 方法数为 30，包含语句块的 signal handler 数为 20，最高 JavaScript 圈复杂度为 9，最高 cognitive complexity 为 15；没有方法超过 CCN 10、cognitive complexity 15 或物理长度 50 行。单个绑定最多关联 10 个成员根，绑定边总数为 590。最高复杂度来自 debug-only 的 `profileScenario`，产品侧最高仍是 `dismissOutside`，CCN 为 8、cognitive complexity 为 8。

当前结构热点不是 JavaScript 复杂度超标，而是对象树和绑定集中：

- `Bubble.qml` 和 `WordsPopup.qml` 的对象树最大深度均为 8。
- `components/DropdownField.qml` 的对象树最大深度为 8，包含 17 个 AST 对象和 55 个 AST 属性绑定。
- `Main.qml::dismissOutside` 是最高的产品 JavaScript 方法复杂度，CCN 为 8，cognitive complexity 为 8；`profileScenario` 是 profiler 自动化入口，不纳入产品复杂度判断。

组件扇入的静态计数如下：`Icon` 11 次，`StatRow` 和 `ShadowCard` 各 8 次，`MenuRow` 4 次，`SwitchRow` 和 `Segment` 各 3 次，`Switch` 和 `DropdownField` 各 1 次。该计数只统计 AST 中以组件类型名出现的对象，不包含通过 singleton 或动态加载建立的依赖。

AST 指标比正则统计更接近真实语法结构，但仍不是 Qt 类型语义分析。`tree-sitter-qmljs` 的已知限制是 grouped binding notation 可能被解析为对象定义，因此对象数量、对象深度和绑定边应作为趋势指标，不应作为绝对架构事实。绑定根目前是语法层的 identifier，不代表已经经过 Qt 类型系统解析的真实依赖。

### 5.3 QML 工具限制

当前 `qmllint 6.9.0` 对 response file 中的 18 个文件产生 0 条结构化诊断，0 个文件失败。`qmllint` 的进程退出码为 0，当前 QML 模块的类型和属性引用已经通过这条检查链。

项目的 `AGENTS.md` 已记录 `controller`、`tray` context property 和 `Screen` 附加类型造成的旧警告背景。当前源码更新后，这些旧诊断已不再出现在结构化结果中；后续仍应以 `qmllint --json` 记录作为诊断指标，而不是使用文本输出总数。

本次没有把 `qmllint` 的警告数量当作 QML 缺陷数量，也没有把目录级执行异常判定为源码质量结论。AST 解析成功也不代表 Qt 类型、绑定生命周期或运行时性能正确，这些仍由 `qmllint` 和后续 profiler 负责。

### 5.4 运行时 profiler 入口

静态 AST 度量完成后，运行时度量使用 Qt 的 `qmlprofiler`。项目新增 `LENS_ENABLE_QML_DEBUG`，默认关闭；只有专用测量构建才打开 QML debugging hooks。当前已有的 `build-ninja` 未启用这个选项，因此本轮没有伪造 profiler 数据。

配置和构建测量版本：

```powershell
cmake --preset ninja-qt6 -DLENS_ENABLE_QML_DEBUG=ON
cmake --build --preset ninja-qt6 --target lens
```

启动 profiler：

```powershell
./scripts/qml-profile.ps1 -BuildDir build-ninja -Output test/records/qml-startup.qtd -Interactive
```

使用 `-Interactive` 后，按 `r` 开始或停止记录，按 `f` 写出并清空当前 trace，按 `q` 结束目标进程。启动应用后应分别记录启动、托盘菜单、设置、气泡、统计、单词和拖动场景；每个场景至少执行 3 轮，再比较 JavaScript、binding、creating、compiling、scenegraph 和 painting 数据。

本次完整流程的首份运行时基线见 [`qml-user-workflow-baseline-2026-10-03.md`](../runtime-performance/qml-user-workflow-baseline-2026-10-03.md)。

无鼠标自动化场景的基线见 [`qml-automated-scenario-baseline-2026-10-03.md`](../runtime-performance/qml-automated-scenario-baseline-2026-10-03.md)。

延迟加载修复后的启动和托盘基线见 [`qml-language-list-lazy-loading-validation-2026-10-03.md`](../runtime-performance/qml-language-list-lazy-loading-validation-2026-10-03.md)。

## 6、其他观察

源码中找到 2 个 `TODO` 标记：

- [`app_controller.cpp:39`](../../../src/app/app_controller.cpp#L39)：等待 TODO.md 中的词书落地。
- [`filter_core.h:78`](../../../src/core/filter_core.h#L78)：等级到排名的映射尚未最终确定。

这两个标记目前都是设计或阶段性说明，不应直接视为未处理缺陷。

## 7、建议处理顺序

1. 优先降低 `lemmatize` 的 CCN，并为每类词形规则保留独立测试。
2. 拆分 `parseExplanations` 的 envelope 校验、结果字段校验和排序恢复。
3. 拆分 `filterWords` 的文本清洗、候选判断和去重阶段。
4. 提取 `KnownStore::load` 的 known 和 cache 解析步骤，保留损坏 JSON 不覆盖原文件的行为。
5. 只有在启动流程继续扩展时，才拆分 `main`；当前不建议为了指标数字强行改动启动顺序。
6. 暂不按行数拆分 `SettingsPopup.qml` 和 `Bubble.qml`，应优先依据界面行为变化和组件复用需求决定。
7. 可以把 `lizard` 加入 CI 做报告或告警；不建议立即把现有热点全部设置为构建失败条件。

## 8、复现命令

在仓库根目录执行 C++ 度量：

```powershell
python -m lizard src -l cpp -Ehalstead -ENS -C 10 -L 50 -a 5
```

执行 QML 度量：

```powershell
./scripts/qml-metrics.ps1
```

执行 AST 结构度量：

```powershell
npm ci
npm run qml:ast-metrics
```

也可以显式指定构建目录并把 JSON 写入已有目录：

```powershell
./scripts/qml-metrics.ps1 -BuildDir build-ninja -Output docs/metrics/qml-metrics.json
```

参数含义：

- `-l cpp`：只分析 C++。
- `-Ehalstead`：计算 Halstead 指标。
- `-ENS`：计算嵌套结构深度。
- `-C 10`：将圈复杂度大于 10 的函数列入警告。
- `-L 50`：将函数长度大于 50 行的函数列入警告。
- `-a 5`：将参数数大于 5 的函数列入警告。

QML 脚本的文件范围来自 `build-ninja/src/app/.rcc/qmllint/*.rsp`。构建目录不存在或未配置 QML response file 时，脚本会失败并提示先配置构建树。

AST 脚本同样自动选择生成的 `.rsp` 文件，依赖固定在 `package.json` 和 `package-lock.json` 中；它不扫描 response file 之外的 QML 文件。

解析运行时 trace：

```powershell
python scripts/parse-qml-trace.py `
  --trace test/records/qml-full-2026-10-03.qtd `
  --output test/records/qml-full-2026-10-03.json
```

本次重新取数时读取了当前工作区的 `src` 修改；新增了 `scripts/qml-metrics.ps1`、`scripts/qml-ast-metrics.js` 和 `scripts/qml-profile.ps1`，没有回退或覆盖这些既有源码修改。AST 已对 18 个 QML 文件完成全量验证；运行时 profiler 尚未运行，因为现有构建未启用 `LENS_ENABLE_QML_DEBUG`。
