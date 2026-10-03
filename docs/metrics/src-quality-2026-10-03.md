# `src` 代码质量度量报告

## 1、报告信息

| 项目 | 内容 |
| --- | --- |
| 分析范围 | `src/` 下的 C++、C++ 头文件和 QML 文件 |
| 生成日期 | 2026-10-03 |
| C++ 分析工具 | `lizard 1.24.0` |
| 分析方式 | C++ 使用函数级静态度量，QML 使用文件级规模和词法近似统计 |
| 第三方代码 | 未纳入 |
| 构建产物 | 未纳入 |

本报告用于定位需要优先审阅的代码热点，不把单个指标当作代码正确性或架构质量的充分证明。

## 2、执行摘要

`src/` 共包含 47 个源码文件，物理行数为 7170 行。其中 C++ 文件 29 个，QML 文件 18 个。

C++ 共分析出 151 个函数，NLOC 为 2693 行，平均圈复杂度为 3.68。整体平均值处于可维护范围，但复杂度集中在少数函数：

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
| `src/app` C++ 和头文件 | 9 | 2147 |
| C++ 合计 | 29 | 4547 |
| `src/app/qml` QML | 18 | 2623 |
| `src` 合计 | 47 | 7170 |

### 3.2 C++ 模块分布

| 模块 | 函数数 | NLOC | 圈复杂度合计 | 平均圈复杂度 |
| --- | ---: | ---: | ---: | ---: |
| `app` | 74 | 965 | 230 | 3.11 |
| `core` | 51 | 630 | 227 | 4.45 |
| `llm` | 26 | 426 | 99 | 3.81 |

`core` 的平均圈复杂度最高，主要由词形处理和持久化读取函数贡献；`app` 的函数和 NLOC 数量最多，主要集中在启动编排、选择抓取和 QML 控制器接口。

## 4、C++ 指标

### 4.1 函数级阈值统计

| 指标 | 结果 | 本次采用的警戒线 |
| --- | ---: | ---: |
| 函数总数 | 151 | - |
| 平均圈复杂度 | 3.68 | 10 |
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
| `lens::core::lemmatize` | [`filter_core.cpp:193`](../../src/core/filter_core.cpp:193) | 76 | 38 | 107 | 47.3 |
| `lens::llm::parseExplanations` | [`llm_pure.cpp:82`](../../src/llm/llm_pure.cpp:82) | 80 | 18 | 102 | 51.9 |
| `lens::core::filterWords` | [`filter_core.cpp:301`](../../src/core/filter_core.cpp:301) | 58 | 27 | 75 | 57.6 |
| `lens::core::KnownStore::load` | [`known_store.cpp:23`](../../src/core/known_store.cpp:23) | 54 | 21 | 64 | 58.4 |
| `main` | [`main.cpp:113`](../../src/app/main.cpp:113) | 60 | 10 | 106 | 59.0 |
| `lens::llm::Pricing::load` | [`llm_pricing.cpp:14`](../../src/llm/llm_pricing.cpp:14) | 49 | 14 | 60 | 62.2 |
| `lens::llm::loadLlmProtocol` | [`llm_protocol.cpp:86`](../../src/llm/llm_protocol.cpp:86) | 48 | 9 | 55 | 63.4 |
| `lens::app::SelectionTextGrabber::grab` | [`selection_text_grabber.cpp:330`](../../src/app/selection_text_grabber.cpp:330) | 54 | 12 | 74 | 64.2 |
| `lens::core::loadIrregulars` | [`filter_core.cpp:140`](../../src/core/filter_core.cpp:140) | 45 | 14 | 52 | 64.3 |

原始 MI 使用本项目度量指南中的公式计算：

```text
MI = 171 - 5.2 × ln（Halstead Volume） - 0.23 × CCN - 16.2 × ln（NLOC）
```

本报告中的 Halstead Volume 来自 `lizard` 的 `-Ehalstead` 扩展。MI 不是 `lizard` 的原生输出，且不同工具对注释、规模和归一化方式的处理不同，因此只用于同一份报告内的相对排序。

### 4.3 热点解释

#### `core::lemmatize`

代码位置：[`src/core/filter_core.cpp:193`](../../src/core/filter_core.cpp:193)。

该函数依次处理前置条件、不规则词、多个后缀规则、候选词生成和频率排名选择。CCN 38 是本次分析的最高值，建议优先拆分为以下内部步骤：

- 不规则词候选选择。
- 后缀规则候选生成。
- 候选词频率比较。

拆分时应保持当前规则顺序和频率优先级不变，并用现有词形测试覆盖每条规则。

#### `llm::parseExplanations`

代码位置：[`src/llm/llm_pure.cpp:82`](../../src/llm/llm_pure.cpp:82)。

该函数同时负责响应 envelope 检查、内容 JSON 检查、结果字段检查、空字段检查、重复词检查和请求顺序恢复。建议将协议层检查、单条结果转换和结果排序分开，使每类失败原因可以独立测试。

#### `core::filterWords`

代码位置：[`src/core/filter_core.cpp:301`](../../src/core/filter_core.cpp:301)。

该函数同时执行空白分词、首尾清洗、粘连字符过滤、长度过滤、大小写过滤、元音过滤、词形归一化、词表过滤、已知词过滤和去重。建议按数据流阶段提取步骤，但不要改变当前短路顺序，因为每个过滤条件都会影响后续的处理成本和结果。

#### `core::KnownStore::load`

代码位置：[`src/core/known_store.cpp:23`](../../src/core/known_store.cpp:23)。

该函数包含文件读取、JSON 顶层校验、等级和语言读取、known 标记读取以及 cache 读取。可以把 `known` 和 `cache` 的解析提取为私有加载步骤，同时保留当前 `文件存在但 JSON 损坏时不重置` 的行为。

#### `main`

代码位置：[`src/app/main.cpp:113`](../../src/app/main.cpp:113)。

`main` 的 CCN 为 10，未达到高复杂度区间，但函数长度达到 106 行。它集中编排日志、数据文件、配置、翻译、QML context property、托盘和鼠标 hook。若启动流程继续增长，建议提取启动阶段函数；当前不必仅为降低指标而重构。

## 5、QML 指标

`lizard` 不识别 `.qml` 文件，因此 QML 只做规模统计和词法近似统计。近似结构分数定义为：

```text
1 + if/for/while 数量 + &&/|| 数量
```

该分数不等同于 QML 的标准圈复杂度，不能与 C++ 的 CCN 直接比较。

### 5.1 文件规模

| 文件 | 物理行 | 非空、非注释行 | 近似结构分数 |
| --- | ---: | ---: | ---: |
| [`SettingsPopup.qml`](../../src/app/qml/SettingsPopup.qml) | 328 | 284 | 4 |
| [`Bubble.qml`](../../src/app/qml/Bubble.qml) | 362 | 279 | 7 |
| [`Main.qml`](../../src/app/qml/Main.qml) | 267 | 199 | 22 |
| [`TrayMenu.qml`](../../src/app/qml/TrayMenu.qml) | 233 | 192 | 3 |
| [`WordsPopup.qml`](../../src/app/qml/WordsPopup.qml) | 223 | 170 | 6 |

QML 合计为 2623 行物理行，其中非空、非注释行约 2086 行，注释行 217 行。`Main.qml` 的近似结构分数最高，主要来自多屏幕定位、点击外部关闭和多个 `Connections` 处理器；它同时也是应用表面协调入口，因此应优先关注行为边界，而不是仅按行数拆分。

### 5.2 QML 工具限制

`qmllint` 可以对单个 QML 文件执行，但当前会对 `controller`、`tray` context property 和 `Screen` 附加类型报告已知的类型解析警告。项目的 `AGENTS.md` 已记录这些警告的来源和现状。

本次没有把 `qmllint` 的警告数量当作 QML 缺陷数量，也没有把目录级执行异常判定为源码质量结论。

## 6、其他观察

源码中找到 2 个 `TODO` 标记：

- [`app_controller.cpp:39`](../../src/app/app_controller.cpp:39)：等待 TODO.md 中的词书落地。
- [`filter_core.h:78`](../../src/core/filter_core.h:78)：等级到排名的映射尚未最终确定。

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

在仓库根目录执行：

```powershell
python -m lizard src -l cpp -Ehalstead -ENS -C 10 -L 50 -a 5
```

参数含义：

- `-l cpp`：只分析 C++。
- `-Ehalstead`：计算 Halstead 指标。
- `-ENS`：计算嵌套结构深度。
- `-C 10`：将圈复杂度大于 10 的函数列入警告。
- `-L 50`：将函数长度大于 50 行的函数列入警告。
- `-a 5`：将参数数大于 5 的函数列入警告。

本报告只新增文档，没有修改 `src`、构建配置或测试代码。报告生成时未运行构建和单元测试，因为本次任务是静态质量度量。
