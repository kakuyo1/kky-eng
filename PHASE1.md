# 阶段一实现规格（Phase 1：单词解释）

> 设计总览见 `DESIGN.md`，术语见 `CONTEXT.md`，UI 规格见 `UI.md`。本文件是阶段一的实施契约：接口先行，UI 完整，未实现功能占位不可触发。

## 1 范围与状态

- **目标**：单词解释通道端到端可用（不含 OCR），UI 各表面完整，未实现功能一律占位、不可触发。
- **取词**：选区（剪贴板监听，真可用）+ `lens_test --filter` 自检。扫描、截图、悬停依赖 OCR，本阶段全部占位。
- **LLM**：真模型直连（DeepSeek，OpenAI 兼容，BYOK）。替代 DESIGN 原 “假 LLM 先跑通” 原型路径，取舍记录见 `DESIGN.md`。
- **状态**：构建脚手架已就位（`CMakeLists.txt` 已改为 `core / llm / app / test` 四目标、`CMakePresets.json` preset `vs-qt6`、`data/wordlist.txt` 88,918 行）；`src/` 尚无源码，四个子目录与 `test/` 均待建。工具链已确认：Qt 6.9.0 MSVC2022_64 @ `B:/qtt/6.9.0/msvc2022_64`。

## 2 非目标（本阶段占位）

| 功能 | 依赖 | 占位方式 |
|---|---|---|
| 扫描（自动模式，稳定快照） | OCR + 前台进程白名单 | 设置「自动扫描」开关禁用 |
| 悬停取词 / 实体通道 | OCR + 词框 | 设置面板不暴露开关；不产生实体气泡 |
| 截图 | OCR | 设置「OCR」开关禁用 |
| 句子通道 | 选区语义 | 不产生句子气泡 |
| 误弹反馈入口 | 反馈闭环 | 浮层不提供该入口（设计见 `CONTEXT.md`「误弹反馈」） |
| 每日预算上限 | 计费统计 | 预算耗尽托盘态不触发 |
| 扫描冷却 / 内容指纹 | 快照管道 | 剪贴板只按「文本不同」防重 |

UI 上占位项：控件存在但 `enabled: false`——文案转 `faint`、开关降透明度、行内跟 10.5 px 弱文字标注「阶段二」，不响应输入。规格见 `UI.md` 4.4。

## 3 单词通道数据流

```
剪贴板变化（选中复制）
  → AppController.onClipboardChanged(text)
  → FilterCore.filterWords（还原 → 正则 → 词表 → 本地判定）
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

判定次序（与 `DESIGN.md` 管道一致）：词根还原 → 正则硬过滤（含元音、长度、无数字，去 URL/邮箱/粘连串）→ 静态词表白名单（不在表即丢）→ 全大写跳过 → known-set 跳过 → 词频阈值跳过。句中首字母大写的普通词本阶段按单词通道处理（实体分类属阶段二，`ponytail:` 简化，验证期观察误弹）。

### 4.2 KnownStore（JSON 持久化，无 Qt）

```cpp
namespace lens::core {

struct WordCache { std::string en, zh, example; };

class KnownStore {
public:
    static KnownStore load(std::filesystem::path path);   // settings.local.json
    bool isKnown(const std::string& lemma) const;
    void mark(const std::string& lemma, bool learned);     // 已会 / 新词
    int level() const;  void setLevel(int);                // 0..7，取值见 CONTEXT.md「档位」
    std::string explanationLang() const;  void setExplanationLang(std::string);  // "en"/"zh"
    std::optional<WordCache> cacheGet(const std::string& lemma) const;
    void cachePut(const std::string& lemma, WordCache);
    void save();
};

}
```

全模块用 `std::string`，JSON 解析走 `third_party/nlohmann/json.hpp`（header-only），因此 `lens_core` 对 Qt 零依赖，可直接被 `lens_test` 离线链接。

单 JSON 文档，加载一次、变更即存。known-set 与缓存均小，暂不上 SQLite（`ponytail:` 到量再迁）。文件同时承载 LLM API 配置（密钥），归属隐私边界（见第 6 节）。

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

`lens_test --filter`：自检样例集（`CONTEXT.md`“自检样例集”）离线跑 FilterCore，断言实际弹出与期望一致。零网络、零密钥，**可进 CI**。
`lens_test --smoke`：发 1 个词走真实 LLM，人工核验 JSON 往返与浮层展示。要 key、要花钱、结论靠人看，**不进 CI**。

## 5 LLM Prompt 与 JSON Schema

目标模型：DeepSeek（OpenAI 兼容）。系统提示（精简、声明式）：

```
你是英语学习工具的释义助手，用户是 CET-4 以上水平的成人学习者。
输入一个单词列表；对每个单词给出最常见的词义：一条英文定义、一条中文释义、一个简短例句。
常见词若有多义，取最常见义项。
输出必须是合法 JSON，符合此结构：
{"results":[{"word":"...","en":"...","zh":"...","example":"..."}]}
必须逐一回显输入单词（原样拼写），不增不减。
```

解释语言设置（英文默认 / 中文）：切换提示词末句为 “英文定义为主、例句用英文” 或 “中文释义为主、例句用中文”；但 `en`/`zh` 两字段始终返回，浮层始终双显（`UI.md` 4.3 规格）。例句暂不进浮层，schema 保留供后续 “难词回顾”。

响应校验 schema（本地解析后逐字段核对，非 LLM 自证）：

```json
{
  "type": "object",
  "required": ["results"],
  "properties": {
    "results": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["word", "en", "zh", "example"],
        "properties": {
          "word":    {"type": "string"},
          "en":      {"type": "string"},
          "zh":      {"type": "string"},
          "example": {"type": "string"}
        }
      }
    }
  }
}
```

## 6 隐私与密钥边界

- API key **仅存本地** `settings.local.json`（gitignored），不提交、不入日志、不写入本会话记忆。输入经设置浮层（掩码显示）或配置文件。
- 发送最小化：单词通道只发单词本身，脱敏兜底；绝不发送整屏或快照全文。
- DEV_SEND_CONFIRM 编译开关：开发期任何发往大模型的请求先弹窗展示内容并征得同意；正式发布整段移除。
- 注意：本次会话中曾粘贴真实 key（已进对话记录），建议开发完成后轮换。

## 7 目录结构与构建目标

```
lens/
├── CMakeLists.txt
├── CMakePresets.json         # preset vs-qt6（Qt 路径 + DEV_SEND_CONFIRM）
├── .clang-format
├── settings.local.json       # 本地密钥与设置，gitignored
├── data/wordlist.txt         # 静态词表（top-100k，词频序，第 8 节）
├── i18n/                     # 每种语言一套 .ts / .qm（待填）
├── icons/                    # 托盘图标，按主题两套（待填）
├── src/
│   ├── core/                 # FilterCore / KnownStore（待建）
│   ├── llm/                  # LlmClient（待建）
│   └── app/                  # AppController + QML 表面（待建）
├── test/                     # 自检入口 + 自检样例集（待建，根目录独立于 src/）
└── ui-prototypes/            # 设计原型（v1-halo-*.html）
```

CMake 目标：`lens_core`（无 Qt）→ `lens_llm` → `lens_app`。`lens_test` 独立于这条链，链接 `lens_core` + `lens_llm`，只在本机构建、不进发布包。

现状：`CMakeLists.txt` 已列出 `src/core`、`src/llm`、`src/app`、`test` 四个目标，但四个目录及其 `CMakeLists.txt` 尚未创建，**动工第一步是把它们建齐，否则 configure 直接失败**。`test/CMakeLists.txt` 定义 `lens_test`，`target_link_libraries` 引用 `lens_core` + `lens_llm`，故必须排在两者之后。

## 8 预检清单（动工前）

| 项 | 状态 |
|---|---|
| Qt 6 + CMake 工具链 | **已完成**：Qt 6.9.0 MSVC2022_64 @ `B:/qtt/6.9.0/msvc2022_64`，preset `vs-qt6` |
| 静态词表 | **已完成**：`data/wordlist.txt` 已落盘 |
| 密钥注入 | **已完成**：`settings.local.json` 已在仓库根且已 gitignore |
| DeepSeek API 核验 | **未做**：用 `context7` 核实模型名、`response_format: {"type":"json_object"}` 支持与 `/chat/completions` 请求形态 |

唯一未完成的预检项是 DeepSeek API 核验，动工前先做掉。

## 9 验证

- FilterCore 单测：样例集离线断言（`lens_test --filter`，零网络，可进 CI）。
- LLM 冒烟：`lens_test --smoke` 单词走真模型，核对 JSON 往返；人工执行，不入 CI。
- 全链手测：复制真实英文句 → 浮层弹词 → [已会]/[新词] 回写 → 复弹不重复。
- 改动中文文档后重跑 zhlint 至零错误。
