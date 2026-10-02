# 阶段一实现规格（Phase 1：单词解释）

> 设计总览见 `DESIGN.md`，术语见 `CONTEXT.md`，UI 规格见 `UI.md`。本文件是阶段一的实施契约：接口先行，UI 完整，未实现功能占位不可触发。

## 1 范围与状态

- **目标**：单词解释通道端到端可用（不含 OCR），UI 各表面完整，未实现功能一律占位、不可触发。
- **取词**：选区（剪贴板监听，真可用）+ `lens_test --filter` 自检。扫描、截图、悬停依赖 OCR，本阶段全部占位。
- **LLM**：真模型直连（DeepSeek，OpenAI 兼容，BYOK）。替代 DESIGN 原 “假 LLM 先跑通” 原型路径，取舍记录见 `DESIGN.md`。
- **状态**：离线内核与 LLM 客户端均已落地——`lens_core`（FilterCore / KnownStore）、`lens_llm`（LlmClient + 纯函数内核）、`lens_test --filter`（样例集 + 存储往返 + LLM 纯函数接缝）与 `lens_test --smoke`（真模型 1 词往返）均通过，见第 9 节。`src/app` 仍为 INTERFACE 占位，AppController 与 QML 表面属后续切片（各片的范围与验收见 §10）。工具链已确认：Qt 6.9.0 MSVC2022_64 @ `B:/qtt/6.9.0/msvc2022_64`，preset `vs-qt6`；`data/wordlist.txt` 88,918 行。

## 2 非目标（本阶段占位）

| 功能                       | 依赖                 | 占位方式                                             |
| -------------------------- | -------------------- | ---------------------------------------------------- |
| 扫描（自动模式，稳定快照） | OCR + 前台进程白名单 | 设置「自动扫描」开关禁用                             |
| 悬停取词 / 实体通道        | OCR + 词框           | 设置面板不暴露开关；不产生实体气泡                   |
| 截图                       | OCR                  | 设置「OCR」开关禁用                                  |
| 句子通道                   | 选区语义             | 不产生句子气泡                                       |
| 误弹反馈入口               | 反馈闭环             | 浮层不提供该入口（设计见`CONTEXT.md`「误弹反馈」） |
| 每日预算上限               | 计费统计             | 预算耗尽托盘态不触发                                 |
| 扫描冷却 / 内容指纹        | 快照管道             | 剪贴板只按「文本不同」防重                           |

UI 上占位项：控件存在但 `enabled: false`——文案转 `faint`、开关降透明度、行内跟 10.5 px 弱文字标注 “阶段二”，不响应输入。规格见 `UI.md` 4.4。

## 3 单词通道数据流

```
剪贴板变化（选中复制）
  → AppController.onClipboardChanged(text)
  → FilterCore.filterWords（分词 → 全大写 → 还原 → 词表 → 档位阈值 → known-set）
  → 候选为空？ 结束。
  → KnownStore 缓存查（lemma + 解释语言）命中？ 直接弹。
  → LlmClient.explainWords（发送前脱敏）
  → [DEV_SEND_CONFIRM 编译开关] 发送预览弹窗确认
  → DeepSeek（一次批量 HTTP，严格 JSON）
  → 响应按 schema 校验 + 回显核对（第三方不可信）
  → 缓存写 → 浮层弹词
  → 用户 [已会]/[新词] → KnownStore 回写
```

阶段一单次复制最多弹 1 个词（最高频候选），5 秒自动消失；鼠标悬浮时计时挂起、永不消失，移出后重新计时。悬停同时展开反馈按钮——规格见 `UI.md`。（多词错峰属多气泡场景，阶段一单气泡不涉及。）

## 4 模块接口（契约先行）

### 4.1 FilterCore（无依赖，可单测）

```cpp
namespace lens::core {

// 静态词表：进程内加载一次，只读（词 → 1-based 词频序）。是「是否真词」的唯一权威判定，
// 词根还原也靠它挑词干。filterWords / lemmatize 要求先调用，否则抛 std::logic_error。
void loadWordlist(const std::filesystem::path& path);

// 词根：running/ran → run。known-set、缓存、词表查询均按 lemma 进行。
std::string lemmatize(std::string_view token);

struct Candidate {
    std::string surface;   // 原文形态（已小写，如 "running"）
    std::string lemma;     // 词根（"run"）
};

// 纯管道，无 I/O。known-set 与词频阈值注入，保持本模块无依赖、可单测。
// 返回按出现顺序去重后的候选；空 = 无需弹词。
std::vector<Candidate> filterWords(
    std::string_view text,
    const std::unordered_set<std::string>& knownLemmas,
    std::size_t minFreqRank);   // 档位 → 词表词频阈值（词书数据未就绪时的近似，见 TODO.md）

}
```

判定次序：分词硬过滤（含元音、长度 ≥3、无数字、去 URL/邮箱/粘连串）→ 全大写跳过 → 词根还原 → 静态词表白名单（不在表即丢）→ 档位词频阈值跳过 → known-set 跳过。句中首字母大写的普通词本阶段按单词通道处理（实体分类属阶段二，`ponytail:` 简化，验证期观察误弹）。

落地注记（2026-10-02，与测试样例集一并定）：

- **词表是模块内加载一次的只读状态**，不占 `filterWords` 参数位：契约里它本就是唯一权威判定，词根还原也要靠它挑词干。故新增 `loadWordlist`；未加载即调用 `filterWords` / `lemmatize` 抛 `std::logic_error`——这是初始化次序 bug，必须响亮而非静默丢弃全部候选。
- **词根还原 = 候选生成 + 词表择一**，不是无脑剥后缀。不规则表命中即裁定；其余由后缀规则产出 “在词表内” 的词干，与原形并列，取词频序最小者。依据是实测：词表同时收录变形词与原形（running 555 / run 314，increasing 7246 / increase 3568，studied 3632 / study 1273），比词频序天然挑中原形；而 water / under / offer 这类词干不是词的原形会安全落回自身。不规则表必须直接返回而非参与比较——children（451）比 child（461）更靠前，按词频挑会挑回 children 自己。
- **不规则表是数据，不是代码**（2026-10-02 改）：`data/irregulars.tsv` 由 `scripts/gen_irregulars.py` 从 WordNet 的四张屈折异常表（`verb/noun/adj/adv.exc`）生成，5761 条，加载走新增的 `loadIrregulars()`——与 `loadWordlist()` 并列，两者都必须先跑，缺一个即抛。此前是 `filter_core.cpp` 里手打的 74 条，覆盖不到 `criteria → criterion`、`cacti → cactus`、`abaci → abacus` 这类后缀规则根本推不出的形式。源表缺 `women → woman`、`people → person`（WordNet 把这两个复数当成独立词条），由生成脚本里一段带注释的补漏补齐。
- **一词多 base 的归属**：表里极少数形式有多个原形（`better` → good / well），本地无从分辨义项，按词表词频择一；单 base 的形式仍命中即裁定。
- **有意的近似**（`ponytail:`，样例集撞出误判再放宽）：`-er` 仅 ≥6 字母、`-est` 仅 ≥7 字母启用，故 nicer（5 字母）与 nicest（6 字母）不还原——**注意 biggest 是 7 字母，规则会触发并经叠辅音减一还原成 big**（早先此处写成 “biggest → big 不还原” 是错的，与代码不符，2026-10-02 更正）；`-ing` / `-ed` 仅 ≥5 字母启用，避开 seed → see 这类伪还原；`news` / `means` 列入 keepAsIs 例外表——它们不在异常表里，需要挡掉 `-s` 规则。
- **分词**：按空白切段，削去首尾既非字母也非数字的字节，剩余内部只要还有非字母字节，整段判粘连串丢弃。数字留在 token 内而不是削掉——否则 version2 会被削成 version 反被弹出。
- **含元音**判定把 `y` 计入，救回 rhythm / myth / gym。
- 去重按 **lemma**（非 surface）：同段内 run 与 running 只留首次出现。

### 4.2 KnownStore（JSON 持久化，无 Qt）

```cpp
namespace lens::core {

struct WordCache { std::string en, zh, example; };

class KnownStore {
public:
    static KnownStore load(std::filesystem::path path);   // settings.local.json
    bool isKnown(const std::string& lemma) const;
    void mark(const std::string& lemma, bool learned);     // 已会 / 新词
    const std::unordered_set<std::string>& known() const;  // 喂 FilterCore::filterWords
    int level() const;  void setLevel(int);                // 0..7，取值见 CONTEXT.md「档位」
    std::string explanationLang() const;  void setExplanationLang(std::string);  // "en"/"zh"
    std::optional<WordCache> cacheGet(const std::string& lemma) const;
    void cachePut(const std::string& lemma, WordCache);
    void save() const;
};

}
```

全模块用 `std::string`，JSON 解析走 `third_party/nlohmann/json.hpp`（header-only），因此 `lens_core` 对 Qt 零依赖，可直接被 `lens_test` 离线链接。

单 JSON 文档，加载一次、变更即存。known-set 与缓存均小，暂不上 SQLite（`ponytail:` 到量再迁）。文件同时承载 LLM API 配置（密钥），归属隐私边界（见第 6 节）。

落地注记（2026-10-02）：

- **保存只覆写自己的键**：从加载时的整份文档出发，改 `level` / `explanationLang` / `known` / `cache` 四项，其余键（`API-KEY` / `URL` 等）原样带回。测试锁死这一点——保存不得吞掉密钥。
- **文件在却读不动 → 抛**，不静默重置：重置后一旦保存，用户的密钥与词库就被覆盖掉了。文件不存在才算首次运行。
- **缓存按（解释语言 + 词根）分键**：`cacheGet` 用当前解释语言查，切语言不串味（`DESIGN.md`“词根 + 语言 → 释义”）。
- `known` 存成 `lemma → bool` 映射（true = 已会 / false = 新词），absent = 未标记；`known()` 是其中 true 的派生视图，喂给 `filterWords` 的 known-set 只认已会——新词按 `CONTEXT.md` 定义仍会再弹。
- `setLevel` 越界抛 `std::out_of_range`；`load` 见越界值回落默认档。
- 档位序号到词频阈值（`filterWords` 的 `minFreqRank`）的映射尚未落地，属 AppController 切片，配合 `TODO.md` 词书数据一起做。

### 4.3 LlmClient（QObject，异步）

```cpp
namespace lens::llm {

struct Config { QUrl baseUrl; QString apiKey; QString model; };
struct WordExplanation { QString word, en, zh, example; };

class LlmClient : public QObject {
    Q_OBJECT
public:
    explicit LlmClient(Config, QObject* parent = nullptr);
signals:
    void batchFinished(QVector<WordExplanation> results);   // 校验通过
    void failed(QString message);                            // 网络 / schema 失败
public slots:
    void explainWords(QStringList words);                    // 一次 HTTP，批量
};

}
```

契约要点：

- 发送前脱敏（邮箱 / 长数字 / URL 掩码）；单词已过硬过滤，此处为兜底。
- **响应是不可信数据**：按 schema 校验 + 输入词逐一回显核对，缺失或多余 → 整体失败，不静默丢词。
- DEV_SEND_CONFIRM 由上层 AppController 拦截（编译开关，发布整段移除），LlmClient 不感知。
- 一次请求上限 20 词；超出静默截断（当前调用方单次仅 1 词，实际不触达）。
- `setExplanationLang(QString)` 为切片二新增，契约原表未列：解释语言在设置浮层里运行时可变，塞进构造期的 `Config` 不合适；它只切 §5 提示词末句。
- 纯函数内核与传输层分离：`src/llm/llm_pure.{h,cpp}` 放脱敏、请求体构造、响应校验三个无网络函数（`lens_test --filter` 覆盖），`llm_client.{h,cpp}` 只剩 QObject + `QNetworkAccessManager`。

落地注记（2026-10-02，DeepSeek 官方文档核验，`settings.local.json` 的 `MODEL` / `URL` 即按此定）：

- **端点**：`POST {baseUrl}/chat/completions`，OpenAI 格式；`baseUrl = https://api.deepseek.com`。配置文件原值 `https://api.deepseek.com/anthropic` 是 Anthropic 格式的 base，对本调用不适用，已改。请求头 `Content-Type: application/json` + `Authorization: Bearer <key>`。
- **模型**：`deepseek-flash`（DeepSeek-V4.1-Flash，支持 JSON Output）。同代另有 `deepseek-v4-pro`；旧名 `deepseek-v4-flash` 仍被接受但模型已停服，请求实际由 V4.1-Flash 承接并按其价计费。
- **必须显式关思考**：DeepSeek 默认开启思考模式（effort=high）。释义任务在请求体加 `"thinking": {"type": "disabled"}`，否则每次多付 reasoning token 且更慢。思考模式下 `temperature` 无效；`top_p` 仅思考模式生效（有效区间 0.95–1.0），非思考模式固定 1.0。
- **`max_tokens` 显式设**：非思考模式缺省 8K。JSON 被截断时接口不报错，只把 `finish_reason` 置为 `length`。
- **`finish_reason` 必查**：仅 `stop` 算成功；`length`（截断）、`content_filter`、`insufficient_system_resource`、`aborted` 一律按整体失败上报——与 “缺失 / 多余 → 整体失败” 同源，响应是不可信数据。
- **错误码归类**：401 认证失败、402 余额不足、429 限流、400 格式错、422 参数错、500 服务端错、503 过载。`failed(message)` 按此分类，**任何分支都不得回显密钥**。

### 4.4 AppController（QML 后端）

```cpp
namespace lens::app {

class AppController : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE void onClipboardChanged(QString text);   // 剪贴板触发入口
    Q_INVOKABLE void mark(QString lemma, bool learned);  // 浮层反馈
    Q_INVOKABLE void setAutoScan(bool on);               // 设置开关 / 全局热键 F8
    Q_PROPERTY(... bubbleModel ...)                       // 当前解释（喂气泡）
    Q_PROPERTY(... settingsModel ...)                     // 档位 / 语言 / API / 触发开关
signals:
    void bubbleReady(QVariantMap payload);                // → QML 弹气泡
    void confirmSendRequest(QStringList words);           // DEV_SEND_CONFIRM 开关
};

}
```

QML 表面：设置浮层、解释气泡、统计弹窗及其下钻的词汇 / 花费弹窗、发送确认，按 `UI.md` 规格落地；占位项 `enabled: false`。托盘图标（四状态）与托盘菜单用 **C++ `QSystemTrayIcon` + `QMenu`**（QtWidgets，Windows 原生可靠；`Qt.labs.platform` 实验性 QML 类型在 6.9 上右键菜单不生效，弃用）。QML 根为隐藏 0×0 `Window`（`visible: false`、`WindowDoesNotAcceptFocus`）——弹层 Window 需要窗口上下文，隐藏窗口无任何可见足迹，不构成主窗口。

### 4.5 Test（`test/`，独立目标 `lens_test`，不编进 `lens_app`）

自检代码与样例集是单独的可执行目标，只在本机构建，**不进发布包**。

`lens_test --filter`：离线自检，三项——**样例集**（`CONTEXT.md`“自检样例集”）跑 FilterCore，断言实际候选与期望一致（顺序、surface、可选 lemma）；**存储往返**跑 KnownStore 在临时文件上的 load / mark / cache / save / reload；**LLM 纯函数接缝**跑脱敏 / 请求体构造 / 响应校验，请求体那项把 §4.3 核验过的 DeepSeek 硬性约束（`json_object`、显式关思考、`max_tokens`）钉成断言。零网络、零密钥，**可进 CI**。

样例集是数据文件 `test/eval_corpus.json`，每段一项：`text`（必填）、`expect`（必填，surface 序列）、`expectLemmas`（选填）、`minFreqRank`（选填，缺省 0）、`known`（选填）、`note`（选填）。改行为先改样例集，`lens_test --filter` 红了再动 `src/`。

`lens_test --smoke [单词]`：发 1 个词走真实 LLM（缺省 `ubiquitous`），人工核验 JSON 往返与浮层展示。要 key、要花钱、结论靠人看，**不进 CI**。密钥从 `settings.local.json` 读入内存后不打印；本命令由人显式敲出，即 DEV_SEND_CONFIRM 的确认本身，故 `LlmClient` 不感知该开关。

## 5 LLM Prompt 与 JSON Schema

线上格式的完整说明（请求体、响应体、校验规则、错误码）见 **`LLM.md`**；这里只记契约要点。

目标模型：DeepSeek（OpenAI 兼容）。**提示词与 schema 都是数据，不是代码**——单词通道的一份在 `data/llm/request.word.json` 与 `data/llm/response.word.schema.json`，改提示词或加字段是改数据，不用重编译。

落地注记（2026-10-02，与切片四一起定）：

- **按通道拆分**：请求属于哪个通道在发送前就定好，通道决定用哪套提示词与哪套响应 schema，故文件名带通道名（`request.<通道>.json` / `response.<通道>.schema.json`）。阶段一只有单词通道落地；实体与句子通道的提示词与响应字段名本契约尚未定义，属阶段二（§2）。
- **系统提示词用英文**，末句由 `{outputLanguage}` 占位符按解释语言替换（`outputLanguage.en` / `outputLanguage.zh`）——只切这一句，其余共用，避免两份提示词各改一半。
- **响应 schema 是校验的唯一真源**：代码从 `properties.results.items.required` 读必填字段名，往 schema 里加字段校验立刻跟着变，文档与代码不会漂移。
- `llm_protocol.{h,cpp}` 负责加载与校验数据文件：缺文件、非法 JSON、必填值为空一律抛，**不回落内置默认值**——回落会正好掩盖这次抽离要防的漂移。

## 6 隐私与密钥边界

- API key **仅存本地** `settings.local.json`（gitignored），不提交、不入日志、不写入本会话记忆。输入经设置浮层（掩码显示）或配置文件。
- 发送最小化：单词通道只发单词本身，脱敏兜底；绝不发送整屏或快照全文。
- DEV_SEND_CONFIRM 编译开关：开发期任何发往大模型的请求先弹窗展示内容并征得同意；正式发布整段移除。
- 注意：本次会话中曾粘贴真实 key（已进对话记录），建议开发完成后轮换。

## 7 目录结构与构建目标

```
lens/
├── CMakeLists.txt
├── CMakePresets.json         # preset ninja-qt6（首选）+ vs-qt6（备选）
├── .clang-format
├── settings.local.json       # 本地密钥与设置，gitignored
├── scripts/build.bat         # 进 VS 环境后驱动 ninja（首配一次，之后纯增量）
├── LLM.md                    # LLM 线上格式说明（请求 / 响应 / 校验 / 错误码）
├── data/wordlist.txt         # 静态词表（top-100k，词频序，第 8 节）
├── data/irregulars.tsv       # 不规则屈折表（WordNet 异常表生成，见 §4.1）
├── data/llm/                 # LLM 协议数据：request.<通道>.json + response.<通道>.schema.json
├── logs/                     # 运行期日志（轮转，gitignored，只留 .gitkeep）
├── third_party/              # 供应商源码：nlohmann/json（header-only）、spdlog（编译成静态库）
├── i18n/                     # 文案翻译：lens_en_US.ts（源）+ lens_zh_CN.ts（中文）
├── icons/                    # 托盘图标，按主题两套（待填）
├── src/
│   ├── core/                 # FilterCore / KnownStore / 日志入口 log.h
│   ├── llm/                  # LlmClient + 纯函数内核
│   └── app/                  # AppController + QML 表面（待建）
├── test/                     # 自检入口 + 自检样例集（根目录独立于 src/）
└── ui-prototypes/            # 设计原型（v1-halo-*.html）
```

CMake 目标：`lens_core`（无 Qt）→ `lens_llm` → `lens_app`。`lens_test` 独立于这条链，链接 `lens_core` + `lens_llm`，只在本机构建、不进发布包。

构建：首选 `ninja-qt6` preset（单配置，增量重编一个源文件约 6 秒），由 `scripts/build.bat` 先进 MSVC 环境再驱动，命令与并行度上限见 `CLAUDE.md`。`vs-qt6` 保留给 IDE。

日志：全项目走 spdlog（`third_party/spdlog`，编译成静态库），模块只用 `src/core/log.h` 的 `LENS_TRACE` / `LENS_DEBUG` / `LENS_INFO` / `LENS_WARN` / `LENS_ERROR` / `LENS_CRITICAL` 宏。`SPDLOG_ACTIVE_LEVEL` 由 CMake 挂在 `lens_core` 上（Debug = trace，Release = info），低于它的调用整条编译掉——不能写在 `log.h` 里，spdlog 自己的 `common.h` 一旦被包含就会抢先定义成 info。`lens::log::init()` 写 `logs/lens.log`（10 MB 一轮，留 3 个备份）并镜像到 stderr，级别可用 `LENS_LOG_LEVEL` 覆盖。Qt 自身的 qDebug / qWarning / qCritical 等由 `src/llm/qt_log.h` 的 `installQtMessageHandler()` 折进同一个 logger，源位置指向 Qt 调用点而非桥接处。密钥永不进日志（第 6 节）。

现状：`lens_core` 为 STATIC（`log.cpp` + `filter_core.cpp` + `known_store.cpp`），`lens_llm` 亦已转 STATIC（`llm_pure.cpp` + `llm_client.cpp`），`lens_test` 链接两者。`src/app` 仍为 INTERFACE 占位（零文件、只挂 Qt 依赖），落地时改成 STATIC 并加源文件即可，`lens_test` 的链接行不用动。

## 8 预检清单（动工前）

| 项                  | 状态                                                                                                                                                                                                                                                                  |
| ------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Qt 6 + CMake 工具链 | **已完成**：Qt 6.9.0 MSVC2022_64 @ `B:/qtt/6.9.0/msvc2022_64`，preset `ninja-qt6`（首选）/ `vs-qt6`（备选）                                                                                                                                               |
| 静态词表            | **已完成**：`data/wordlist.txt` 已落盘                                                                                                                                                                                                                        |
| 密钥注入            | **已完成**：`settings.local.json` 已在仓库根且已 gitignore                                                                                                                                                                                                    |
| DeepSeek API 核验   | **已完成**（2026-10-02）：模型名 `deepseek-flash` / `deepseek-v4-pro`；`response_format: {"type":"json_object"}` 支持，且要求 prompt 含 `json` 字样与格式示例；OpenAI 格式端点为 `{baseUrl}/chat/completions`。结论与落地细节见 §4.3 落地注记、§5。 |

预检四项已全部完成，可以动工。核验渠道说明：本机未装 `uv` / `mcp` 包、也没有 `CONTEXT7_API_KEY`，`context7` 不可用；改直接抓 DeepSeek 官方文档（`api-docs.deepseek.com`，国内直连可达，且是更权威的一手来源）。

## 9 验证

- FilterCore 单测：样例集离线断言（`lens_test --filter`，零网络，可进 CI）。**已通过**：样例集 8 段 + 存储往返，覆盖档位词频阈值、词根还原（后缀规则与 WordNet 异常表）、只有表能覆盖的形式（`criteria → criterion` 一类）、垃圾内容（URL / 邮箱 / 文件名 / 带数字串 / 连字 / 全大写）、不在词表即丢、同段同词根去重、known-set 跳过。
- LLM 纯函数接缝：脱敏 / 请求体 / 响应校验三项离线断言（`lens_test --filter`）。**已通过**：覆盖邮箱、URL、长数字掩码；`model` / `stream:false` / `response_format` / `thinking:disabled` / `max_tokens` / 提示词含 `json` 与格式示例 / 解释语言切末句；`finish_reason != stop`、外层非 JSON、缺 `results`、字段缺失、回显错词 / 漏词 / 多余词一律整体失败。断言经变异验证确实会红。
- LLM 冒烟：**已通过**（2026-10-02）：`--smoke ubiquitous` 经 `deepseek-flash` 真实往返，`word` / `en` / `zh` / `example` 四字段回显与 §5 schema 一致，`finish_reason=stop`。人工执行，不入 CI。
- 全链手测：复制真实英文句 → 浮层弹词 → [已会]/[新词] 回写 → 复弹不重复。
- 改动中文文档后重跑 zhlint 至零错误。

## 10 切片计划

切片 = 一次可独立验收的落地单元。编号此前只活在临时交接单里（那份删了就没了），在此固定下来；本文件是它的唯一出处。

### 切片一：离线内核（已完成）

- **范围**：词表加载、词根还原、候选过滤、本地状态持久化，全离线可自检。
- **交付**：`src/core/filter_core.{h,cpp}`、`src/core/known_store.{h,cpp}`、`data/wordlist.txt`、`test/` 的样例集与存储往返。
- **验收**：`lens_test --filter` 全绿，零网络、零密钥。

### 切片二：LLM 客户端（已完成）

- **范围**：真模型直连（DeepSeek，OpenAI 兼容，BYOK）。传输层 `LlmClient`（QObject + `QNetworkAccessManager`）与无网络内核（脱敏 / 请求体构造 / 响应校验）分离。
- **交付**：`src/llm/llm_client.{h,cpp}`、`src/llm/llm_pure.{h,cpp}`、`--smoke` 真模型往返。
- **验收**：`--smoke` 单词往返四字段齐全；响应校验对畸形输入一律整批拒绝。

### 工程基建（已完成，不占切片号）

这一段不在原计划里，是几次按需请求累积出来的，单独记一笔以免来历不明：spdlog 与 `LENS_*` 日志宏（Qt 消息并入同一 logger）、Ninja 构建与 `scripts/build.bat`、全项目英文 Doxygen 注释、i18n 骨架（`i18n/*.ts`，英文为源语言）、LLM 线上协议数据化（`data/llm/` + `LLM.md`）、不规则屈折表数据化（`data/irregulars.tsv`）。

### 切片三：AppController + QML 表面（未开始）

- **范围**：`src/app` 落地。`AppController`（剪贴板入口、候选查缓存、known-set 回写、设置读写、DEV_SEND_CONFIRM 拦截）与 `UI.md` 的全部表面（设置浮层、解释气泡、统计弹窗及其下钻的词汇 / 花费弹窗、发送确认）；托盘图标四状态与菜单用 **C++ `QSystemTrayIcon` + `QMenu`**。
- **含**：§4.2 遗留的 “档位序号 → 词频阈值” 映射，与 `TODO.md` 的词书数据一起做（`minFreqRank` 目前是近似，见 §4.1 落地注记）。
- **含**：i18n 首次真正生效——QML 目录加入 `lupdate` 扫描，`.qm` 经 CMake 构建、启动时加载（现在 `.ts` 只有 llm 模块的 22 条，加载机制尚未接）。
- **验收**：复制真实英文句 → 浮层弹词 → [已会] / [新词] 回写 → 复弹不重复；占位项 `enabled: false` 不可触发；界面语言中英切换生效。

### 切片四：阶段二通道（未开始）

- **范围**：OCR 取词（扫描 / 悬停 / 截图）、实体通道、句子通道、误弹反馈入口、每日预算上限（§2 全部占位项）。
- **前置**：`data/llm/request.<通道>.json` 与 `response.<通道>.schema.json` 的提示词与响应字段名要先有契约。通道骨架（`Channel` 枚举、按通道加载器、`setChannel`）已就位，内容未定。
