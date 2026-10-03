# 阶段一实现规格（Phase 1：单词解释）

> 设计总览见 `DESIGN.md`，术语见 `CONTEXT.md`，UI 规格见 `UI.md`。本文件是阶段一的实施契约：接口先行，UI 完整，未实现功能占位不可触发。

## 1 范围与状态

- **目标**：单词解释通道端到端可用（不含 OCR），UI 各表面完整，未实现功能一律占位、不可触发。
- **取词**：选区（低层鼠标钩子监听拖选松手，注入 Ctrl+C 取剪贴板，真可用）+ `lens_gtest_unit` 自检。扫描、截图、悬停依赖 OCR，本阶段全部占位。
- **LLM**：真模型直连（DeepSeek，OpenAI 兼容，BYOK）。替代 DESIGN 原 “假 LLM 先跑通” 原型路径，取舍记录见 `DESIGN.md`。
- **状态**：切片一（离线内核）、切片二（LLM 客户端）与切片三（AppController + QML 表面）均已落地。`lens_core`（FilterCore / KnownStore / StatsStore）、`lens_llm`（LlmClient + 纯函数内核 + 价目）、`src/app`（捕获组件 + AppController + 托盘 + QML 七个表面）齐备，可执行目标 `lens` 已能起来。`lens_gtest_unit` 29 例全绿，见第 9 节；冒烟走 `lens_gtest_smoke`，需人手动执行。工具链已确认：Qt 6.9.0 MSVC2022_64 @ `B:/qtt/6.9.0/msvc2022_64`，preset `ninja-qt6`（首选）；`data/wordlist.txt` 88,918 行。切片四（阶段二通道）未开始。

## 2 非目标（本阶段占位）

| 功能                       | 依赖                 | 占位方式                                             |
| -------------------------- | -------------------- | ---------------------------------------------------- |
| 扫描（自动模式，稳定快照） | OCR + 前台进程白名单 | 设置「自动扫描」开关禁用                             |
| 悬停取词 / 实体通道        | OCR + 词框           | 设置面板不暴露开关；不产生实体气泡                   |
| 截图                       | OCR                  | 设置「OCR」开关禁用                                  |
| 句子通道                   | 选区语义             | 不产生句子气泡；动作条上的翻译 / 解释会以浮层说明无可用通道 |
| 误弹反馈入口               | 反馈闭环             | 浮层不提供该入口（设计见`CONTEXT.md`「误弹反馈」） |
| 每日预算上限               | 计费统计             | 预算耗尽托盘态不触发                                 |
| 扫描冷却 / 内容指纹        | 快照管道             | 选区文本只按「文本不同」防重                         |

UI 上占位项：控件存在但 `enabled: false`——文案转 `faint`、开关降透明度、不响应输入，且不加任何文字说明。产品界面上不出现阶段号，也不用 “即将支持” 这类话，用户只看到不可用。规格见 `UI.md` 4.4。

## 3 单词通道数据流

```
拖选松手（低层鼠标钩子 WH_MOUSE_LL：LBUTTONUP 且按下期间确实拖动过，双击 / 三击选词选段同理）
  → AppController.onSelectionReleased(anchor)：注入 Ctrl+C → 读剪贴板（存还原）→ 选区类型判定
     （判定的唯一入口是 FilterCore.classifySelection：出候选 = Word；无候选 = Sentence，
       句子 / 实体通道属阶段二，见 §2）
  → 选区动作条弹出（翻译 / 解释 / 复制文本。前两项是同一决策的两个入口，见 `UI.md` §4.9）
  → 复制文本 → 本地结束，不发请求。
  → 翻译 / 解释 → 同一条路，通道由上面判的类型定；阶段一只有 Word 走得通
  → Sentence：不发请求，以浮层说明没有可解释的单词（动作条三项一律可用，见 §4.4）
  → KnownStore 缓存查（lemma + 解释语言）命中？ 直接弹。
  → LlmClient.explainWords（发送前脱敏）
  → [DEV_SEND_CONFIRM 编译开关] 发送预览弹窗确认
  → DeepSeek（一次批量 HTTP，严格 JSON）
  → 响应按 schema 校验 + 回显核对（第三方不可信）
  → 缓存写 → 浮层弹词（锚点同上）
  → 用户 [已会]/[新词] → KnownStore 回写
```

阶段一单次复制最多弹 1 个词（`filterWords` 的首个候选，即选区内最先出现的那个），5 秒自动消失；鼠标悬浮时计时挂起、永不消失，移出后重新计时。悬停同时展开反馈按钮——规格见 `UI.md`。（多词错峰属多气泡场景，阶段一单气泡不涉及。）无候选时动作条照样弹出，三项一律可用；翻译与解释不发请求，改由浮层说明没有可解释的单词——动作条点完即自隐，若此处静默，读者看到的是一次毫无反应的点击（`UI.md` §4.9 不灰化，反馈只能落在 “点了之后”）。

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

// 选区是什么，决定它走哪条通道。阶段二的实体通道并入本枚举。
enum class SelectionKind {
    Word,      // 出了候选：word 通道有东西可发
    Sentence,  // 无候选：阶段二的句子 / 实体通道接管
};

struct Selection {
    SelectionKind kind = SelectionKind::Sentence;
    std::vector<Candidate> candidates;   // 非 Word 时为空
};

// 类型判定的唯一入口。出参同时带着候选，免得再跑一遍 filterWords，也给阶段二的实体分支
// 留出唯一的落点。按钮不参与判定：翻译与解释是同一决策的两个入口（`UI.md` §4.9）。
Selection classifySelection(
    std::string_view text,
    const std::unordered_set<std::string>& knownLemmas,
    std::size_t minFreqRank);

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

struct WordCache { std::string en, zh; };

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

统计三个弹窗（统计 / 词汇 / 花费）要的数据此前没有归属，切片三补上（2026-10-03）。`StatsStore` 与 `KnownStore` **共用 settings.local.json，但不自己读写文件**：构造时绑定 `KnownStore::document()` 交出的那份文档，只写 `history` / `daily` 两键，落盘仍由 `KnownStore::save()` 一次写完。两个类各自持一份文档的话，后存的一方会把先存的一方的改动整体覆盖——用户的标记或密钥就这么没了，所以文档的所有者只能有一个。

```cpp
namespace lens::core {

/// @brief A word the bubble showed, kept for the words popup.
struct HistoryEntry {
    std::string lemma;
    std::string minute;   ///< Local "YYYY-MM-DD HH:MM".
    std::string verdict;  ///< "" until marked, then "known" or "new".
};

/// @brief One day's tallies, for the stats and cost popups.
struct DailyUsage {
    int pops = 0;
    int learned = 0;
    int fresh = 0;
    long long promptTokens = 0;
    long long completionTokens = 0;
};

class StatsStore {
public:
    explicit StatsStore(nlohmann::json& document);   // the document KnownStore owns

    void recordPop(std::string lemma, std::string minute);
    void recordVerdict(const std::string& lemma, std::string minute, std::string verdict);
    void recordUsage(const std::string& minute, long long promptTokens, long long completionTokens);

    const std::vector<HistoryEntry>& history() const;        ///< newest first
    const std::map<std::string, DailyUsage>& daily() const;  ///< keyed by local date
};

}
```

落地注记（2026-10-03）：

- **只存 token，不存金额**。单价会变，把当时的金额刻进历史没有意义；金额在展示时由 token × 当前价目算出（§4.3 的 `llm_pricing`），改价不改历史记录的结构。
- **`history` 有上限**：留最近 2000 条，超出按时间截断最旧的，否则文档随使用无界增长。`daily` 是按日聚合，一条一天，不设上限。
- **日期与分钟都取本地时间**：词汇弹窗的三档时间（`今天 14:20` / `昨天 21:48` / `09-30 14:02`）由界面按日期差选格式，故存 `YYYY-MM-DD HH:MM` 原样，不做相对化。
- **`recordVerdict` 找最新一条同词且未标记的记录回填**；找不到就只动当日计数——标记可能发生在弹词之后很久，两次之间又弹过别的词。

### 4.3 LlmClient（QObject，异步）

```cpp
namespace lens::llm {

struct Config { QUrl baseUrl; QString apiKey; QString model; };
struct WordExplanation { QString word, en, zh; };
struct Usage { int promptTokens = 0; int completionTokens = 0; };  // 响应 usage 字段

class LlmClient : public QObject {
    Q_OBJECT
public:
    explicit LlmClient(Config, QObject* parent = nullptr);
signals:
    void batchFinished(QVector<WordExplanation> results, Usage usage);  // 校验通过
    void failed(QString message);                                       // 网络 / schema 失败
public slots:
    void explainWords(QStringList words);                               // 一次 HTTP，批量
};

}
```

契约要点：

- 发送前脱敏（邮箱 / 长数字 / URL 掩码）；单词已过硬过滤，此处为兜底。
- **响应是不可信数据**：按 schema 校验 + 输入词逐一回显核对，缺失或多余 → 整体失败，不静默丢词。
- DEV_SEND_CONFIRM 由上层 AppController 拦截（编译开关，发布整段移除），LlmClient 不感知。
- 一次请求上限 20 词；超出静默截断（当前调用方单次仅 1 词，实际不触达）。
- `setExplanationLang(QString)` 为切片二新增，契约原表未列：解释语言在设置浮层里运行时可变，塞进构造期的 `Config` 不合适；它只切 §5 提示词末句。
- 纯函数内核与传输层分离：`src/llm/llm_pure.{h,cpp}` 放脱敏、请求体构造、响应校验三个无网络函数（`lens_gtest_unit` 覆盖），`llm_client.{h,cpp}` 只剩 QObject + `QNetworkAccessManager`。

落地注记（2026-10-02，DeepSeek 官方文档核验，`settings.local.json` 的 `MODEL` / `URL` 即按此定）：

- **端点**：`POST {baseUrl}/chat/completions`，OpenAI 格式；`baseUrl = https://api.deepseek.com`。配置文件原值 `https://api.deepseek.com/anthropic` 是 Anthropic 格式的 base，对本调用不适用，已改。请求头 `Content-Type: application/json` + `Authorization: Bearer <key>`。
- **模型**：`deepseek-flash`（DeepSeek-V4.1-Flash，支持 JSON Output）。同代另有 `deepseek-v4-pro`；旧名 `deepseek-v4-flash` 仍被接受但模型已停服，请求实际由 V4.1-Flash 承接并按其价计费。
- **必须显式关思考**：DeepSeek 默认开启思考模式（effort=high）。释义任务在请求体加 `"thinking": {"type": "disabled"}`，否则每次多付 reasoning token 且更慢。思考模式下 `temperature` 无效；`top_p` 仅思考模式生效（有效区间 0.95–1.0），非思考模式固定 1.0。
- **`max_tokens` 显式设**：非思考模式缺省 8K。JSON 被截断时接口不报错，只把 `finish_reason` 置为 `length`。
- **`finish_reason` 必查**：仅 `stop` 算成功；`length`（截断）、`content_filter`、`insufficient_system_resource`、`aborted` 一律按整体失败上报——与 “缺失 / 多余 → 整体失败” 同源，响应是不可信数据。
- **错误码归类**：401 认证失败、402 余额不足、429 限流、400 格式错、422 参数错、500 服务端错、503 过载。`failed(message)` 按此分类，**任何分支都不得回显密钥**。
- **usage 随结果一起回报**（2026-10-03，切片三）：`batchFinished` 带 `Usage`，由 `parseUsage()` 从同一个响应里读 `usage.prompt_tokens` / `usage.completion_tokens`。**usage 缺失按 0 计并记警告，不判整批失败**——统计少一笔可以忍，把一次成功的解释整批丢掉不行。这一点与 “缺失 / 多余 → 整体失败” 不冲突：那条管的是解释内容，这条管的是计费元数据。
- **价目是数据**：`data/llm/pricing.json` 按模型名给输入 / 输出单价（每百万 token）与币种，`llm_pricing.{h,cpp}` 加载并算 `Usage -> 金额`。改价是改数据，不重编译；价目缺失或模型不在表内时金额记 0 并记警告，不影响解释。
- **牌价与展示币种分开**（2026-10-03）：表里 0.15 / 0.6 是厂商的**美元**牌价，界面显示人民币。做法是 `display` 块写明目标币种与一个乘数（`multiplier`）及其取值日期（`asOf`），**牌价原样不动**——把 `currency` 直接改成 `CNY` 等于宣称厂商按人民币报价，那是说错。换算只在 `Pricing::cost()` 里发生一次，`currency()` 返回展示币种，故统计、花费、托盘提示三处无需各自知道还有另一种币。乘数是数据，会过期：启动时把乘数与日期一起记进日志，看到 ¥ 想问 “哪来的” 时不必翻文件。`display` 块缺失时展示币种回落到厂商币种。

### 4.4 AppController（QML 后端）

接口不再用 `...Model` 占位，切片三落成下面这份（2026-10-03）。喂给 QML 的一律是 `QVariantMap` / `QVariantList` 只读属性，QML 只读不写；写回一律走 `Q_INVOKABLE`。

```cpp
namespace lens::app {

class AppController : public QObject {
    Q_OBJECT
public:
    AppController(core::KnownStore& store, llm::LlmClient& llm, MouseSelectionHook& hook,
                  const llm::Pricing& pricing, QObject* parent = nullptr);

    void onSelectionReleased(QPoint anchor);             // 选区入口：鼠标钩子在拖选松手时调用

    Q_INVOKABLE void runSelectionAction(QString action, QString text);  // 动作条回传：translate / explain / copy
    Q_INVOKABLE void mark(QString lemma, bool learned);  // 浮层反馈
    Q_INVOKABLE void setAutoScan(bool on);               // 设置开关 / 全局热键 F8
    Q_INVOKABLE void setLevel(int level);
    Q_INVOKABLE void setExplanationLang(QString lang);
    Q_INVOKABLE void setTheme(QString theme);            // "light" / "dark"
    Q_INVOKABLE void setUiLanguage(QString lang);        // "zh" / "en"，切 .qm
    Q_INVOKABLE void setSelectionCapture(bool on);       // 阶段一唯一有实效的触发开关
    Q_INVOKABLE void setApiKey(QString key);             // 只写文件，不回显

    Q_INVOKABLE void bubbleHoverChanged(bool hovering);  // QML 持有 5 秒计时，这里只记状态
    Q_INVOKABLE void dismissBubble();

    Q_INVOKABLE void confirmSend();                      // DEV_SEND_CONFIRM 的两条出路，
    Q_INVOKABLE void cancelSend();                       // 正式构建里随开关整段消失

    Q_PROPERTY(QVariantMap bubble READ bubble NOTIFY bubbleChanged)      // 当前解释，空 map = 无气泡
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)         // 今日计数 + 历史累计
    Q_PROPERTY(QVariantList words READ words NOTIFY statsChanged)        // 词汇弹窗的行
    Q_PROPERTY(QVariantMap cost READ cost NOTIFY statsChanged)           // 花费弹窗的各档金额
    Q_PROPERTY(QString modeLabel READ modeLabel NOTIFY settingsChanged)  // 托盘首行与 tooltip 共用
    Q_PROPERTY(QString busyLabel READ busyLabel NOTIFY busyChanged)      // 托盘 icon 的「解释中」态

signals:
    void selectionBarRequested(QVariantMap payload);      // → QML 弹选区动作条：{x, y, kind, text}
    void bubbleChanged();
    void settingsChanged();
    void statsChanged();
    void busyChanged();                                   // 托盘 icon 切「解释中」
    void uiLanguageChanged(QString lang);                 // → 启动处换 QTranslator
    void confirmSendRequest(QStringList words);           // DEV_SEND_CONFIRM 开关
};

}
```

落地注记（2026-10-03）：

- **`bubble` 与 `settings` 都是 map，不是 QObject 模型**。表面数量个位数、字段都是标量，为每个表面写一个 QAbstractItemModel 是给 QML 添一层没人问的间接。
- **5 秒计时归 QML**：自动消失与悬停挂起是视图行为（`UI.md` §4.3），计时的持有者在 QML；`bubbleHoverChanged` 只让 C++ 知道状态，不参与计时。否则计时器要跨进程边界地和悬停事件对齐。
- **`modeLabel` 一个属性喂两处**：托盘菜单首行与 tooltip 都写同一个 “Lens · 模式 · 今日词数” 串（`UI.md` §4.2 / §4.5），分两处拼字符串必然漂移。
- **`cost` 的五个数在 C++ 算**：本月、今天、昨天、本周、日均里的日期运算用 `QDate`，core 侧只存 token 与按日计数（§4.2）。
- **气泡不带音标**：`UI.md` §4.3 的单词行右侧有音标位，而响应 schema 只有 `word` / `en` / `zh` 三字段（§5），阶段一不放音标位。
- **类型判定从 `beginSelection` 里提出来**（2026-10-03）：判定原本是行内一句 `candidates.empty()`，现收到 `core::classifySelection`（§4.1），出参是 `SelectionKind` 加候选。两条理由：它要可单测，而 `lens_gtest_unit` 不链接 `lens_app`，所以只能落在 `lens_core`；阶段二的实体分支需要一个唯一的落点，各写各的分支正是这一条要挡掉的。`beginSelection` 改按 `switch (kind)` 取值而非再看一次 `empty()`——枚举穷尽时编译器会在加通道那一刻报错，这正是要的。
- **物理 → 设备无关那次换算其实一直是空操作**（2026-10-03 更正 §4.4 “锚点是物理像素” 那条）：`Main.qml` 的 `toDip()` 形如 `payload.x = payload.x / ratio`，而 `payload` 是 `controller.bubble` 这类 `QVariantMap` 属性交出来的 JS 对象——**赋值被接受、读回来还是旧值**。实测（临时 `console.log`，已删）：`toDip` 里 `ratio=1.25`、`in=936,248`，同一对象走到 `Bubble.show()` 里仍是 `936,248`。所以自这条注记写下起，每个表面都落在 1.25 倍的目标位置上：气泡跑到选区右侧约 234 物理像素，副屏上的动作条干脆掉到屏幕底边。改法是**造一个新对象**再返回，`payload` 的其他字段原样拷过去。同一处坏掉的还有 `dismissOutside(toDip(at))`——“点外面关闭” 一直拿物理坐标去比设备无关的卡片矩形。
- **气泡的高度要在布局之后再量一次**（2026-10-03）：`Bubble.show()` 原来在同一次调用里算完位置，而 `height` 是内容的，刚赋过 `text` 的 `Text` 还没布局——实测这一步读到 85，卡片实际占 132。卡片于是按 “矮” 定位，随后向下长过它本应让开的选区。现在先摆一次（这一帧要画的东西），再 `Qt.callLater(place, payload)` 按真实高度摆第二次。同一机制对 `SelectionBar` 无影响：它三项固定、高度不随内容变。
- **显示一张表面是两步**（2026-10-03）：`visible = true` 不够，还要 `raise()`。实机反馈是托盘菜单被任务栏压住一角——`placePanel` 把卡片的右下角对齐图标中心，卡片下缘本就在 48 DIP 任务栏里进了一半，而点托盘图标会把任务栏**激活**，它同样是置顶窗口且会把自己重新提到置顶带顶端，于是刚显示出来的面板落在它下面。三处显示路径（`Main.qml` 的 `placePanel`、`Bubble.show`、`SelectionBar.openAt`）都补上 `raise()`，没有共用的辅助函数：三个文件各自只有这一行，为它造一个上下文属性不划算。既知边界：这改的是**谁在上面**，不是**摆哪儿**——`placeBeside` 仍按整屏夹取，卡片照旧与任务栏重叠，只是现在盖在它上面。
- **气泡改与动作条同侧**（2026-10-03）：气泡原规格是挂在锚点**下方**（`UI.md` §4.3 旧文），动作条在**上方**，于是同一个 “选区完成” 手势引出的两张卡片一上一下。现改为两张都往上排，上方放不下才翻到下方，`Bubble.qml` 的 `show()` 与 `SelectionBar.qml` 的 `openAt()` 用同一条公式。两处都按**卡片**算，`shadowMargin` 各自减掉（§4.4 的 “贴合按卡片算” 那条）。既知边界：那条翻转判据是 `above >= 0`，即拿屏幕绝对坐标 0 当上边界，副屏（负 y）上的选区永远判为 “上方没地方” 而一律翻到下方——动作条与气泡现在同病。
- **无通道时不再静默**（2026-10-03）：`explain()` 原先对非 word 选区直接 `return`，而动作条 `onTapped` 点完即 `visible = false`，于是选一段非单词文本点翻译 / 解释，看到的是**动作条自己消失、什么都没发生**，读起来就是 “只有复制能用”——尽管解释在那段选择上同样什么都没做。现在走 `showNotice()`，与请求失败同一条路：气泡无 status chip、zh 放说明、标题放选区文本（句子没有单个词可命名，空标题会让卡片悬在半空）。判定悬停展开的已会 / 新词按钮加了一条前提 `status !== ""`——通知的标题不是词，在那里按下会把整句当作 lemma 写进词库。`UI.md` §4.9 的 “三项一律可用、不灰化” 保持不变，反馈只能落在点之后，这是那条规格的直接推论。

落地注记（2026-10-02）：

- **触发是选区完成，不是剪贴板变化**：Windows 没有 API 能直接读到别的应用里被选中的文字，所以入口定为低层鼠标钩子（`WH_MOUSE_LL`，落在 `src/app/mouse_selection_hook.{h,cpp}`）：`LBUTTONUP` 且按下期间确实拖动过（双击选词、三击选段同理）即算选区完成，回调 `onSelectionReleased(anchor)`。取文靠 `SendInput` 向当前前台应用注入 Ctrl+C 再读剪贴板——这是覆盖最广的一条路，凡能复制的应用都通（含 PDF 阅读器）。锚点就是松手坐标，不必再拿 `QCursor::pos()` 近似。
- **两个必须处理的副作用**：一是剪贴板被顶掉——注入前存、读完还原，其间用户恰好复制的东西会被吞（`ponytail:` 竞争窗口，真被投诉再上 UIA TextPattern 绕开剪贴板取文）；二是注入的 Ctrl+C 在终端里就是 SIGINT——按前台进程名排除终端类（Windows Terminal / conhost / PowerShell），与 `DESIGN.md` 的扫描白名单同源。
- **注入前必须确认前台不是自己**：靠 §4.4 已有的 `WindowDoesNotAcceptFocus`——动作条与气泡都不夺焦点，点它们不会污染下一次注入的目标。
- **动作条介入数据流**：`onSelectionReleased` 不再直接通向气泡，中间隔着选区动作条。回传的 `action` 取 `translate` / `explain` / `copy`，前两者行为相同（通道由类型定，见 `UI.md` §4.9），故 `action` 只用来区分 “本地复制” 与 “发 LLM 请求” 两条路。
- **捕获组件是两个文件**：`mouse_selection_hook.{h,cpp}`（低层钩子与手势规则）与 `selection_text_grabber.{h,cpp}`（注入 Ctrl+C、剪贴板存还原）。终端排除与前台自查都落在取文那一步——危险发生在注入处，那也才是查得到前台进程的地方；`selectionReleased` 只带 `QPoint`，不带进程名。两条纯谓词 `isSelectionGesture` / `isExcludedProcess` 收普通参数，可离线断言。
- **锚点是物理像素，窗口坐标不是**：钩子拿到的 `pt` 从不虚拟化，而 QML 窗口落在设备无关像素上，两者差一个 `devicePixelRatio`（本机 125% 实测：物理 x=275 的松手位置，窗口坐标是 220）；按 1:1 直接用，动作条会偏出选区四分之一屏，渲染循环还会记 “矩形不与屏幕相交”。Qt 会把 `QGuiApplication` 设成 per-monitor aware（实测进程感知级别 = 2），这一条只决定渲染清晰度，不改变上面的换算——交给动作条与气泡之前必须先除。

落地注记（2026-10-03，表面第二轮）：

- **`QSystemTrayIcon::geometry()` 给的是设备无关像素，不是物理像素**，而且任务栏自动隐藏时不报值。实测：把任务栏勾出来时返回 `(1313, 816, 32, 48)`——1313 DIP 对应物理 1641，正是通知区左缘，48 DIP 是那 60 物理像素的任务栏厚度。任务栏一收回去它就变 0，而弹窗都是从托盘菜单点开的，点的那一刻任务栏已经收回，于是每个面板都落到硬编的右下角、再被 `- width + 60` 推出屏外。`Tray` 现在记住最后一次非空的值：窗口不动，过期的答案也是答案。
- **定位与边缘检测合成一处**：`Main.qml` 的 `placeBeside()` 把卡片的右下角放在图标**中心**，按锚点所在那屏把窗口整块夹进屏内，上方放不下就翻到下方。四张面板的 `openNear` 与 `TrayMenu.openAt` 因此全部删掉，菜单与面板走同一条路。
- **贴合按卡片算，不按窗口**：每个表面都是卡片加四边各 26 px 透明阴影边距的窗口，早先拿窗口去贴锚点，卡片实际偏出一个边距（气泡纵向还多偏一个 gap），看着就是没贴住选区。气泡与动作条的 `openAt` 现在把 `shadowMargin` 减掉再算。
- **钩子必须认自己的窗口**：按下落在本进程的窗口上时，这次按下既不是选区手势，也不算双击的第一击（`WindowFromPoint` + `GetWindowThreadProcessId` 比对 PID）。否则在面板上拖动会被当成拖选，松开时注入 Ctrl+C，把背后应用里的选中内容当作读者选的词弹出来。代价写在函数注释里：透明的阴影边距也算自己的窗口，贴着面板 26 px 内起手的真选区会被放掉。
- **主屏的任务栏是自动隐藏的**，`GetSystemMetrics(0/1)` 只给主屏尺寸；要整块虚拟桌面得用 76 / 77 / 78 / 79 四个索引。
- **标题行的图标贴右锚定，不用固定占位**：原先的 `Item { width: parent.width - 40 }` 是按英文标题估的，中文标题一变宽就把关闭按钮整个挤出卡片外（放大实测）。改成左锚标题、右锚图标行。
- **字体族设一次，设在 `main.cpp`，不设在 QML**：QML 的 `font.family` 只收一个名字（原先写的是三段逗号串，Qt 当成一个名字找，找不到就整站落到 Tahoma——`Text.fontInfo` 在真窗口上读回来的就是这个），而列表属性 `font.families` 在 QML 的 font 值类型上根本不存在（赋值即 `Cannot assign to non-existent property "families"`）。所以 `QGuiApplication::setFont` 收一个 `QFont::setFamilies({"Segoe UI Variable", "Microsoft YaHei UI Light"})`，QML 侧**不再写字体族**，靠继承。中文必须点名落到 Light：Microsoft YaHei UI 的常规体比 Segoe UI Variable 重一档，同权重下中文标签看着像加了粗，按墨量实测才定的案。代价是等宽那几个 `Text` 一旦写 `font.family: Tokens.monoFamily` 就丢掉这条回落，字符串里的中文（128 词的那个单位）走系统回落、比周围略重，只有三处。
- **拖动用指针自己的屏幕位置**：`controller.cursorPos()`，也就是 `QCursor::pos()`。另外两条路都实测过、都不行。系统移动循环（`startSystemMove()`）在整个拖动过程里没动过窗口，松手才落位，那是跳不是拖；handler 的 `activeTranslation` 更糟，它量的是**窗口内**的偏移，移动窗口就改变了决定这次移动的那个值——按手速拖几十个事件，面板从 x=1116 被甩到 x=-3688。两个坑记在这里：`QCursor::pos()` **返回的已经是 DIP**，照着钩子那套再除一次 1.25 会让拖动只走 1/1.25 的距离（实测少走 27%）；节拍用 16 ms 定时器而不是 `activeTranslationChanged`，因为窗口一旦跟上指针偏移就不再变化、信号随之停止，剩下那段位移永远不会被应用。松手那一拍再补一次定位。

QML 表面：设置浮层、选区动作条、解释气泡、统计弹窗及其下钻的词汇 / 花费弹窗、发送确认，按 `UI.md` 规格落地；占位项 `enabled: false`。托盘图标（四状态）用 **C++ `QSystemTrayIcon`**：那是 shell 的东西，没有 QML 对应物。**菜单不是原生的**——`Menu` 曾按 “Windows 原生可靠” 选过 `QMenu`（`Qt.labs.platform` 的实验性 QML 类型在 6.9 上右键菜单不生效，仍弃用），代价是画不成 `UI.md` §4.2 的卡片；现改为一个普通表面（`qml/TrayMenu.qml`），`Tray` 只在图标被点时发一个 `menuRequested`。QML 根为隐藏 0×0 `Window`（`visible: false`、`WindowDoesNotAcceptFocus`）——弹层 Window 需要窗口上下文，隐藏窗口无任何可见足迹，不构成主窗口。

### 4.5 Test（`test/googletest/`，独立目标，不编进 `lens_app`）

测试代码与样例集是单独的可执行目标，只在本机构建，**不进发布包**：它是 `lens_core`（无 Qt）→ `lens_llm` → `lens_app` 这条链之外的旁支，`lens_gtest_unit` / `lens_gtest_smoke` 链接 `lens_core` + `lens_llm`，`lens_gtest_perf` 只链接 `lens_core`（保持无 Qt），`lens_gtest_integration` 链接 `lens_app` + `lens_core`。

契约要点只有一条：**样例集 `test/eval_corpus.json` 是 FilterCore 的行为规格**——改行为先改样例集（`CONTEXT.md`“自检样例集”），测试红了再动 `src/`。离线那一支零网络、零密钥，**可进 CI**。

框架、目录、三个目标、样例集字段表与运行方式见 `TEST.md`。

### 4.6 Profiling（`src/core/profile.{h,cpp}`，零 Qt）

`lens_core` 内的测量点，由 CMake 选项 `LENS_ENABLE_PROFILE` 做编译期开关。三条契约约束：

- **零 Qt**，与 §4.2 一致。
- **不逐条打日志**。热路径按调用记日志会淹掉日志本身，并盖住要观察的现象；站点只累加，热阶段结束后用一次 `report()` 出汇总表。
- **计时器不是免费的**，所以两个原语并存：作用域计时器给粗粒度，计数器给每 token 量分母。先用计数器，再决定要不要给内层计时。

原语定义、开销实测、profile 构建树与记录约定见 `TEST.md` §4。

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
- **档位与界面语言不进请求**：档位只影响本地词表与 known-set 预置（见 `CONTEXT.md` 档位条目）。2026-10-02 的系统提示词曾写进 “CET-4 level or above”，属违规，已删；提示词只说取最常见义项，判断全在本地。
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
├── TEST.md                   # 测试：框架 / 目标 / 样例集 / profiling / 记录
├── data/wordlist.txt         # 静态词表（top-100k，词频序，第 8 节）
├── data/irregulars.tsv       # 不规则屈折表（WordNet 异常表生成，见 §4.1）
├── data/llm/                 # LLM 协议数据：request.<通道>.json + response.<通道>.schema.json + pricing.json
├── logs/                     # 运行期日志（轮转，gitignored，只留 .gitkeep）
├── third_party/              # 供应商源码：nlohmann/json（header-only）、spdlog 与 googletest（编译成静态库）
├── i18n/                     # 文案翻译：lens_en_US.ts（源）+ lens_zh_CN.ts（中文）
├── icons/                    # 托盘图标，深浅任务栏两套（SVG）；预算态已备图、未接线
├── src/
│   ├── core/                 # FilterCore / KnownStore / StatsStore / 日志入口 log.h / 测量点 profile.h
│   ├── llm/                  # LlmClient + 纯函数内核 + 价目
│   └── app/                  # 捕获组件 + AppController + 托盘 + main + qml/（表面在根，组件在 qml/components/）
├── test/                     # googletest/ 下的 unit / perf / smoke，以及样例集（独立于 src/）
└── ui-prototypes/            # 设计原型（v1-halo-*.html）
```

CMake 目标：`lens_core`（无 Qt）→ `lens_llm` → `lens_app`。四个 `lens_gtest_*` 独立于这条链，只在本机构建、不进发布包：`unit` 与 `smoke` 链接 `lens_core` + `lens_llm`，`perf` 只链接 `lens_core`（保持无 Qt），`integration` 链接 `lens_app` + `lens_core`。

构建：首选 `ninja-qt6` preset（单配置，增量重编一个源文件约 6 秒），由 `scripts/build.bat` 先进 MSVC 环境再驱动，命令与并行度上限见 `AGENTS.md`。`vs-qt6` 保留给 IDE。

日志：全项目走 spdlog（`third_party/spdlog`，编译成静态库），模块只用 `src/core/log.h` 的 `LENS_TRACE` / `LENS_DEBUG` / `LENS_INFO` / `LENS_WARN` / `LENS_ERROR` / `LENS_CRITICAL` 宏。`SPDLOG_ACTIVE_LEVEL` 由 CMake 挂在 `lens_core` 上（Debug = trace，Release = info），低于它的调用整条编译掉——不能写在 `log.h` 里，spdlog 自己的 `common.h` 一旦被包含就会抢先定义成 info。`lens::log::init()` 写 `logs/lens.log`（10 MB 一轮，留 3 个备份）并镜像到 stderr，级别可用 `LENS_LOG_LEVEL` 覆盖。Qt 自身的 qDebug / qWarning / qCritical 等由 `src/llm/qt_log.h` 的 `installQtMessageHandler()` 折进同一个 logger，源位置指向 Qt 调用点而非桥接处。密钥永不进日志（第 6 节）。

现状：`lens_core` 为 STATIC（`log.cpp` + `profile.cpp` + `filter_core.cpp` + `known_store.cpp` + `stats_store.cpp`），`lens_llm` 亦为 STATIC（`llm_pure.cpp` + `llm_client.cpp` + `llm_protocol.cpp` + `llm_pricing.cpp`），`lens_gtest_unit` / `lens_gtest_smoke` 链接两者，`lens_gtest_perf` 只链接 `lens_core`。`src/app` 为 STATIC，装着捕获组件（§4.4）、`AppController` 与托盘图标（`QSystemTrayIcon`；菜单是 QML 表面），除 `Qt6::Core` 外挂 Gui / Widgets 与 `user32`。可执行目标 `lens`（同目录的 `main.cpp` + `qml/`）不与 `lens_gtest_*` 共用：`qt_add_qml_module` 挂在 `lens` 上而不是静态库上——挂静态库要额外处理 QML 插件注册，而测试目标本来就不需要 QML。

## 8 预检清单（动工前）

| 项                  | 状态                                                                                                                                                                                                                                                                  |
| ------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Qt 6 + CMake 工具链 | **已完成**：Qt 6.9.0 MSVC2022_64 @ `B:/qtt/6.9.0/msvc2022_64`，preset `ninja-qt6`（首选）/ `vs-qt6`（备选）                                                                                                                                               |
| 静态词表            | **已完成**：`data/wordlist.txt` 已落盘                                                                                                                                                                                                                        |
| 密钥注入            | **已完成**：`settings.local.json` 已在仓库根且已 gitignore                                                                                                                                                                                                    |
| DeepSeek API 核验   | **已完成**（2026-10-02）：模型名 `deepseek-flash` / `deepseek-v4-pro`；`response_format: {"type":"json_object"}` 支持，且要求 prompt 含 `json` 字样与格式示例；OpenAI 格式端点为 `{baseUrl}/chat/completions`。结论与落地细节见 §4.3 落地注记、§5。 |

预检四项已全部完成，可以动工。核验渠道说明：本机未装 `uv` / `mcp` 包、也没有 `CONTEXT7_API_KEY`，`context7` 不可用；改直接抓 DeepSeek 官方文档（`api-docs.deepseek.com`，国内直连可达，且是更权威的一手来源）。

## 9 验证

- FilterCore 单测：样例集离线断言（`lens_gtest_unit`，零网络，可进 CI）。**已通过**：样例集 8 段 + 存储往返，覆盖档位词频阈值、词根还原（后缀规则与 WordNet 异常表）、只有表能覆盖的形式（`criteria → criterion` 一类）、垃圾内容（URL / 邮箱 / 文件名 / 带数字串 / 连字 / 全大写）、不在词表即丢、同段同词根去重、known-set 跳过。
- LLM 纯函数接缝：脱敏 / 请求体 / 响应校验三项离线断言（`lens_gtest_unit`）。**已通过**：覆盖邮箱、URL、长数字掩码；`model` / `stream:false` / `response_format` / `thinking:disabled` / `max_tokens` / 提示词含 `json` 与格式示例 / 解释语言切末句；`finish_reason != stop`、外层非 JSON、缺 `results`、字段缺失、回显错词 / 漏词 / 多余词一律整体失败。断言经变异验证确实会红。
- 迁移核对：**已通过**（2026-10-02）：手写 `CHECK` 转入 gtest 后为 11 例（样例集 1 + KnownStore 6 + 掩码 1 + LLM 纯函数 3），`lens_gtest_unit` 全绿。原 `main.cpp`（452 行）删除。
- LLM 冒烟：**已通过**（2026-10-02，迁移前的手写版本）：`ubiquitous` 经 `deepseek-flash` 真实往返，`word` / `en` / `zh` 三字段回显与 §5 schema 一致，`finish_reason=stop`。迁移后的 `lens_gtest_smoke` 尚未跑过真实往返，待人工执行。
- FilterCore 热点：**已测量**（2026-10-02，RelWithDebInfo，`LENS_ENABLE_PROFILE=ON`，样例集 8 段 × 200 轮，`filterWords` 1600 次 / `lemmatize` 8400 次 / token 10800 个）。**`lemmatize` 只占一部分，其余落在分词与硬过滤阶段**，比此前 “热路径即 lemmatize” 的说法更宽。五轮数据（见 `TEST.md` 的记录约定）：`lemmatize` 很稳，1.85–2.04 ms（221–240 ns/次）；`filterWords` 在 4.36–7.04 ms 之间摆，占机器干扰最大的分词侧。因此占比是区间而非点值：**最干净的三轮约 43%，受干扰时降到 27%**。同株关掉插桩作对照，插桩整体抬高约 16%，反推一次计时器约 60 ns；扣掉后下限约 36%。
- 一次运行的定量结论都带这类区间，跑 `lens_gtest_perf` 时至少看三轮。
- 选区捕获（2026-10-03）：`lens_gtest_integration` 15 例中 14 例通过、1 例跳过。机器已验证——手势规则（含阈值边界、双击 / 三击、反向拖动）、终端排除名单的大小写与全路径匹配、钩子装上后能收到拖拽并按松手坐标发出锚点（拖拽由 `SendInput` 合成，低层钩子对合成事件与真实事件一视同仁）、前台是自己时取文拒绝执行，以及剪贴板存还原的三面（内容被顶掉后还原、非文本格式一并还原、没取过快照时不许动剪贴板）。真人手测过一次（2026-10-03，Windows 记事本 11.2607 商店版）：注入的 Ctrl+C 确实取到了选区，十字相符——契约里那句 “凡能复制的应用都通” 有实证了。同一次的日志还给出了逐格式拷贝的量化理由：记事本一次 Ctrl+C 往剪贴板放了 4 种格式，快照全数取回并全数还原，**只存文本会毁掉其中 3 种**。
- **剪贴板的存还原用 OLE 是错的**（2026-10-03 实测推翻）：`OleGetClipboard` 取出的 `IDataObject` 交给 `OleSetClipboard` 一律失败，`CLIPBRD_E_CANT_CLOSE` 或 `CLIPBRD_E_CANT_OPEN`；中间有没有变化、内容由本进程还是别的进程（`clip.exe` 验过）放入，结果都一样，而同一次运行里 `OleSetClipboard(nullptr)` 却成功，所以坏的是对象往返而非 setter。现在的做法是裸开剪贴板逐格式读出字节，还原时空盘再逐格式写回；位图 / 调色板 / 增强图元文件 / owner-display 族这类句柄格式按名跳过并记日志；`ole32` 不再需要。这条错误原先只有人工用例能碰，改成三个离线用例后当场复现。
- 全链手测：复制真实英文句 → 浮层弹词 → [已会]/[新词] 回写 → 复弹不重复。
- 切片三（2026-10-03）：`lens_gtest_unit` 29 例全绿——切片一的 11 例之外，新增 `StatsStore` 8 例（往返、判定回填、上限截断、畸形文档、与 `KnownStore` 共用文档时的互不覆盖）、`parseUsage` 3 例、价目 5 例（含展示币种与乘数）、`classifySelection` 2 例。
- 切片三界面：`lens` 起来无 QML 警告，七个表面在真实桌面渲染核对过（截图）：选区动作条、解释气泡、统计 / 词汇 / 花费三弹窗、设置浮层。界面语言切到中文后各表面文案为中文（`i18n` 共 84 条、`lrelease` 报 0 unfinished）；托盘图标资源加载成功（`QIcon::isNull()` 为假）。占位项按 §2 灰化且不响应。
- **切片三的端到端验收尚未跑**：三条验收里 “复制真实英文句、浮层弹词、[已会] / [新词] 回写、复弹不重复” 这条需要真实鼠标操作与一次真实的模型往返，属人手动，未执行。因此 “选区取词在真机上从手势走到气泡” 这条链目前只有各段的证据，没有整条的证据。
- 改动中文文档后重跑 zhlint 至零错误。

## 10 切片计划

切片 = 一次可独立验收的落地单元。编号此前只活在临时交接单里（那份删了就没了），在此固定下来；本文件是它的唯一出处。

### 切片一：离线内核（已完成）

- **范围**：词表加载、词根还原、候选过滤、本地状态持久化，全离线可自检。
- **交付**：`src/core/filter_core.{h,cpp}`、`src/core/known_store.{h,cpp}`、`data/wordlist.txt`、`test/` 的样例集与存储往返。
- **验收**：`lens_gtest_unit` 全绿，零网络、零密钥。

### 切片二：LLM 客户端（已完成）

- **范围**：真模型直连（DeepSeek，OpenAI 兼容，BYOK）。传输层 `LlmClient`（QObject + `QNetworkAccessManager`）与无网络内核（脱敏 / 请求体构造 / 响应校验）分离。
- **交付**：`src/llm/llm_client.{h,cpp}`、`src/llm/llm_pure.{h,cpp}`、`lens_gtest_smoke` 真模型往返。
- **验收**：`lens_gtest_smoke` 单词往返四字段齐全；响应校验对畸形输入一律整批拒绝。

### 工程基建（已完成，不占切片号）

这一段不在原计划里，是几次按需请求累积出来的，单独记一笔以免来历不明：spdlog 与 `LENS_*` 日志宏（Qt 消息并入同一 logger）、Ninja 构建与 `scripts/build.bat`、全项目英文 Doxygen 注释、i18n 骨架（`i18n/*.ts`，英文为源语言）、LLM 线上协议数据化（`data/llm/` + `LLM.md`）、不规则屈折表数据化（`data/irregulars.tsv`）。

第二次追加（2026-10-02，同样不占切片号）：GoogleTest 进 `third_party` 并退役手写自检（§4.5）、`src/core/profile.{h,cpp}` 与 `LENS_ENABLE_PROFILE` 选项（§4.6）。两件都是基建，没有可独立验收的用户交付物，故按上一段的先例记在这里，不另起切片号。

### 切片三：AppController + QML 表面（进行中）

- **范围**：`src/app` 落地。`AppController`（选区入口：鼠标钩子 + Ctrl+C 取文、选区类型判定与锚点、动作条回传、候选查缓存、known-set 回写、设置读写、DEV_SEND_CONFIRM 拦截）与 `UI.md` 的全部表面（设置浮层、选区动作条、解释气泡、统计弹窗及其下钻的词汇 / 花费弹窗、发送确认、托盘菜单）；托盘图标四状态用 **C++ `QSystemTrayIcon`**，菜单是一个表面（见 §4.4 的落地注记）。
- **含**：§4.2 遗留的 “档位序号 → 词频阈值” 映射，与 `TODO.md` 的词书数据一起做（`minFreqRank` 目前是近似，见 §4.1 落地注记）。
- **含**：i18n 首次真正生效——QML 目录加入 `lupdate` 扫描，`.qm` 经 CMake 构建、启动时加载（现在 `.ts` 只有 llm 模块的 22 条，加载机制尚未接）。
- **含**：统计三个弹窗的数据层 `StatsStore`（§4.2）与金额所需的 `Usage` + 价目（§4.3）——此前两个契约里都没有，是切片三补的。
- **验收**：复制真实英文句 → 浮层弹词 → [已会] / [新词] 回写 → 复弹不重复；占位项 `enabled: false` 不可触发；界面语言中英切换生效。

落地注记（2026-10-03）：

- **主窗口不存在，根窗口却必须真的可见**。表面各自是一个 `Window`（`UI.md` 的 “各表面相互独立，不共用窗口” 正是这么写的），不是 `Popup`；而 QML 里嵌在另一个 `Window` 内的 `Window` 会成为它的 transient child，Windows 在父窗口隐藏时不会把 transient child 显示出来——原先写的 “0×0 且 `visible: false`” 实测所有表面都不出现，改成 1×1、`opacity: 0`、带 `WindowTransparentForInput` 的可见窗口后正常（挪到屏幕外也管用，但渲染循环会为 “矩形不与任何屏幕相交” 每次启动记一条警告）。这一条靠跑起来验证，不从文档推。
- **窗口的屏幕坐标是 `x` / `y`**：`Window` 没有 `screenX` / `screenY`（那是 `Item` 的属性），在函数里写未加限定的 `screenY = ...` 会去写全局属性并报错。
- **QML 的 `font.pixelSize` 是整数**：`UI.md` 字号表里的 12.5 / 11.5 / 10.5 px 落不了地，按四舍五入取 13 / 12 / 11，保住 “最小 11 px” 那条约束。
- **选区的类型判定发生在动作之前**：`onSelectionReleased` 走完取文只判定并弹动作条，不请求；点 “翻译” 或 “解释” 之后才按判定结果分流，单词发请求，无候选时只有复制文本这一条路走得通。判定结果放进 `selectionBarRequested` 的 `kind`，QML 不参与判定。
- **发出去的是词根，不是原文形态**：选中 running 时，请求、缓存键、气泡标题、历史记录统一用 lemma（run）。§4.2 已定缓存按词根分键，若请求发 surface，同一次选择就会产生 “问的是 running、缓存存的是 run” 两套键，再选 ran 也命中不了。代价是气泡标题显示词典形而非读者选中的词形。
- **托盘 icon 只有三个可达状态**：自动扫描开 / 解释中 / 已关；第四态（预算耗尽）要等每日预算上限落地，它的图已在 `icons/` 里备好但没有分支去选它——留一个到不了的分支比缺一张图更坏。

真机跑过之后的修正（2026-10-03，同样不占切片号）：

- **`MultiEffect` 不能包着文字用**：它把源渲进一张离屏贴图，本机 125% 下这张贴图被重采样——实测同一个字形笔画从 5 像素的锐利边变成 11 像素的糊团，同一个窗口里直接画的那份是清晰的。阴影改由一份只有形状、`visible: false` 的副本去投（`ShadowCard.qml` / `SelectionBar.qml` / `Bubble.qml`），文字走直接绘制。新增表面照此办理，不要为了阴影把内容塞进特效层。
- **同理，特效 item 只能用源的尺寸**：`MultiEffect` 把自己那份源**铺满整个 item**，所以写成 `anchors.fill: parent`（parent 是整个窗口，比卡片大）时，卡片的形状会被拉伸到整窗——实测每个弹窗底部都多出一层背景，就是这个。要么像现在这样只给位置、让特效按源自己定尺寸（`x: card.x; y: card.y`，不写 width / height），要么让 item 与源同尺寸。
- **动作条与气泡此前偏出选区**：见 §4.4 的锚点修正，`main.qml` 的 `toDip()` 是唯一换算点，钩子给的物理像素一律先过它。
- **`lens` 目标此前是 console 子系统**：托盘应用带一个常驻黑窗口，且 `WIN32_EXECUTABLE` 关着，Qt 也就不会链 `Qt6::EntryPoint`（`Qt6::Core` 的接口按这个属性用生成器表达式取舍）。一行 `set_target_properties(lens PROPERTIES WIN32_EXECUTABLE ON)` 同时解决两件事，不必手写 manifest、也不必手动声明 DPI 感知。
- **界面语言要调 `QQmlApplicationEngine::retranslate()`**：装翻译器不会让 QML 的 `qsTr` 绑定重算，托盘菜单当场变是因为 C++ 那侧自己接了 `uiLanguageChanged`，QML 表面没有对应动作。因此 `engine` 必须声明在接这个信号的 lambda 之前。
- **换语言时信号的顺序不是随意的**：`setUiLanguage` 先发 `uiLanguageChanged`（它才装翻译器），再发 `settingsChanged` / `statsChanged`。反过来发，重算 `words()` 时用的还是旧翻译器，词汇弹窗会出现表头已变、行没变的样子——实测就是这样，先发 `statsChanged` 那一版没修好。
- **点击外部关闭靠钩子**：表面各是独立 `Window`，落在别的窗口上的按下根本不会送进本进程，只有低层钩子看得见（`MouseSelectionHook::pointerPressed` → `AppController` 转发 → `main.qml` 的 `dismissOutside()`）。判 “外面” 用的是卡片矩形而非窗口矩形，四周 26 px 阴影边距算外面。
- **鼠标钩子跑在自己的线程上**（2026-10-03）：`WH_MOUSE_LL` 的回调在**安装它的那个线程**上执行，Windows 让鼠标等这个线程应答，等满 `LowLevelHooksTimeout`（默认 300 ms）就放弃——那个线程只要有一段时间不抽消息，鼠标就整段卡住。原先钩子装在主线程上，而主线程要做的启动工作（建托盘图标、编译 QML、建九个窗口、第一帧的图形初始化）里没有一处抽消息。实测办法是另一进程循环注入 `mouse_event` 并给每次调用计时（脚本在 `%TEMP%\mouse-stall-probe.ps1`，未入库）：**连续三次各 311.9 / 311.6 / 311.7 ms**，正好压在 300 ms 超时上，这就是读者感到的 “鼠标被抢走”。先把 `install()` 挪到 `app.exec()` 前一行，降到一次 227 ms——不够，因为第一帧的图形初始化就在 exec 里。现在钩子装在自己的线程上（`hookThreadMain`：先 `PeekMessage` 让本线程有消息队列，再 `SetWindowsHookEx`，然后 `GetMessage` 泵到底），主线程再怎么卡都与鼠标无关；同一探针复测 **0 次超 60 ms，最坏 27.7 ms**。发送路径随之从 `QTimer::singleShot` 改为 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`：回调在钩子线程上，信号必须投递到拥有表面的线程，跨线程排队正是 Qt 为这件事提供的；`MouseSelectionHook` 本身仍活在主线程，所以队列落在主线程上。析构走 `PostThreadMessage(WM_QUIT)` 加 `join`，`UnhookWindowsHookEx` 由钩子线程在退出路上调用——只有它能知道没有回调在途。实机复验两条跨线程信号：拖选 → 动作条弹出，点外面 → 动作条关闭。
- **界面出现前仍有约 1.5 s，其中 `loadFromModule` 占 810–923 ms**（2026-10-03）：临时埋点（已删）测出这一段几乎全在 `engine.loadFromModule("Lens", "Main")` 一个调用里——`QQmlApplicationEngine` 构造 20 ms、两个 context property 0 ms、装翻译器 0 ms。数据侧另算：`wordlist` 229 ms + `irregulars` 24 ms。钩子搬走之后这段时间**不再冻结鼠标**，所以它只是 “起得慢”，不是卡顿；里面在花什么时间尚未查清（QML 已由 `qmlcachegen` 预编译，九个窗口的创建与 SVG 图标的栅格化都还没被单独量过）。
- **`qml/` 下按 “是不是窗口” 分两处**（2026-10-03）：九个 `Window`（表面）留在 `qml/` 根，其余九件可复用件（`Icon` / `ShadowCard` / `Tokens` / `Segment` / `StatRow` / `MenuRow` / `DropdownField` / `Switch` / `SwitchRow`）进 `qml/components/`。两组同属一个 QML 模块（`qt_add_qml_module` 的 `QML_FILES` 里写子目录路径即可），Qt 给模块内每个文件隐式导入本模块的类型，**跨目录照样按类型名解析，谁也不需要写 import**——实机验过动作条与气泡照常渲染。判据取 “是不是窗口” 而非 “被几处用到”：`Switch` / `MenuRow` / `DropdownField` 今天各只被一处使用，它们仍是组件，而按使用次数切会把同类东西拆到两边。九个搬走的文件里没有一处 `qsTr`（文案一律由表面传入），所以两份 `.ts` 一行未动，只有 CMake 的路径要跟着改。

### 切片四：阶段二通道（未开始）

- **范围**：OCR 取词（扫描 / 悬停 / 截图）、实体通道、句子通道、误弹反馈入口、每日预算上限（§2 全部占位项）。
- **前置**：`data/llm/request.<通道>.json` 与 `response.<通道>.schema.json` 的提示词与响应字段名要先有契约。通道骨架（`Channel` 枚举、按通道加载器、`setChannel`）已就位，内容未定。
