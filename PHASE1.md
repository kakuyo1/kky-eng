# 阶段一实现规格（Phase 1：单词、实体与句子解释）

> 设计总览见 `PRODUCT.md`，术语见 `GLOSSARY.md`，UI 规格见 `UI.md`。本文件是阶段一的实施契约：接口先行，UI 完整，未实现功能占位不可触发。

## 1 范围与状态

- **目标**：单词、实体与句子解释通道端到端可用（不含 OCR），UI 各表面完整，未实现功能一律占位、不可触发。
- **取词**：选区（低层鼠标钩子监听拖选松手，注入 Ctrl+C 取剪贴板，真可用）+ `lens_gtest_unit` 自检。扫描、截图、悬停依赖 OCR，本阶段全部占位。
- **LLM**：真模型直连（DeepSeek，OpenAI 兼容，BYOK）。替代 PRODUCT 原 “假 LLM 先跑通” 原型路径，取舍记录见 `PRODUCT.md`。
- **状态**：切片一（离线内核）、切片二（LLM 客户端）、切片三（AppController + QML 表面）与 V3 的实体 / 句子通道均已落地。`lens_core`（FilterCore / KnownStore / StatsStore）、`lens_llm`（LlmClient + 纯函数内核 + 价目）、`src/app`（捕获组件 + AppController + 托盘 + QML 表面）齐备，可执行目标 `lens` 已能起来。冒烟走 `lens_gtest_smoke`，需人手动执行。工具链已确认：Qt 6.9.0 MSVC2022_64 @ `B:/qtt/6.9.0/msvc2022_64`，preset `ninja-qt6`（首选）；`data/wordlist.txt` 88,918 行。

## 2 非目标（本阶段占位）

| 功能                       | 依赖                 | 占位方式                                             |
| -------------------------- | -------------------- | ---------------------------------------------------- |
| 扫描（自动模式，稳定快照） | OCR + 前台进程白名单 | 设置「自动扫描」开关禁用                             |
| 悬停取词                    | OCR + 词框           | 设置面板不暴露开关；不产生悬停实体气泡                 |
| 截图                       | OCR                  | 设置「OCR」开关禁用                                  |
| 误弹反馈入口               | 反馈闭环             | 浮层不提供该入口（设计见`GLOSSARY.md`「误弹反馈」） |
| 每日预算上限               | 计费统计             | 预算耗尽托盘态不触发                                 |
| 扫描冷却 / 内容指纹        | 快照管道             | 选区文本只按「文本不同」防重                         |

UI 上占位项：控件存在但 `enabled: false`——文案转 `faint`、开关降透明度、不响应输入，且不加任何文字说明。产品界面上不出现阶段号，也不用 “即将支持” 这类话，用户只看到不可用。规格见 `UI.md` 4.4。

## 3 单词通道数据流

```
拖选松手（低层鼠标钩子 WH_MOUSE_LL：LBUTTONUP 且按下期间确实拖动过，双击 / 三击选词选段同理）
  → AppController.onSelectionReleased(anchor)：注入 Ctrl+C → 读剪贴板（存还原）→ 选区类型判定
      （判定的唯一入口是 FilterCore.classifySelection：多 token 名称短语（Title Case 或全大写缩写）= Entity；
        整个选区是单个字母 token：在词表 = Word，不在词表 = Entity；其余（多 token）= Sentence。
        候选带 New / Known / Mastered 状态，后两者只标注不删，见 §4.1）
  → 选区动作条弹出（翻译 / 解释 / 复制文本。前两项是同一决策的两个入口，见 `UI.md` §4.10）
  → 复制文本 → 本地结束，不发请求。
   → 翻译 / 解释 → 同一条路，通道由上面判的类型定；Sentence 再按动作选择 preset
   → Entity：实体通道；Sentence：句子通道（翻译 / 解释选择对应预设）
   → Word：KnownStore 缓存查（lemma + 解释语言）命中？直接弹
   → Entity / Sentence：只发送原选区文本，不查单词缓存
   → LlmClient.explainWords（发送前脱敏）
  → DeepSeek（一次批量 HTTP，严格 JSON）
  → 响应按 schema 校验（word 另做逐词回显核对；entity / sentence 的 title 由 App 盖上）（第三方不可信）
   → Word：缓存写 → 浮层弹词 → 用户 [已会]/[新词] → KnownStore 回写
   → Entity：浮层弹名字 + 百科式释义；Sentence：浮层弹「句子」标签 + 译文 / 讲解（不画原句）。两者都不显示 IPA / verdict，不写入单词缓存与词汇统计
```

单次选区按形状分流：整个选区是单个字母 token 时，在静态词表里走 word（查一个词），不在词表里走 entity（命名实体，百科式说明）；多 token 走实体 / 句子，整段翻译或解释。5 秒自动消失；鼠标悬浮时计时挂起、永不消失，移出后重新计时。悬停只对单词解释展开 [已会] / [新词] 按钮——规格见 `UI.md`。（多词错峰属多气泡场景，当前单气泡不涉及。）动作条三项一律可用：句子的翻译 / 解释分别选择对应预设（随解释语言改变语义，见 §5），实体的两个按钮共用同一预设，复制始终本地结束。

## 4 模块接口（契约先行）

### 4.1 FilterCore（无依赖，可单测）

```cpp
namespace lens::core {

// 静态词表：进程内加载一次，只读（词 → 1-based 词频序）。是「是否真词」的唯一权威判定，
// 词根还原也靠它挑词干。filterWords / lemmatize 要求先调用，否则抛 std::logic_error。
void loadWordlist(const std::filesystem::path& path);

// 词根：running/ran → run。known-set、缓存、词表查询均按 lemma 进行。
std::string lemmatize(std::string_view token);

// 候选与读者的关系：新词 / 读者标过已知 / 落在档位词频带内。本模块只标注，由调用方
// 决定怎么用——选区取词取首个新词，阶段二的自动扫描才整体跳过后两类。
enum class CandidateState { New, Known, Mastered };

struct Candidate {
    std::string surface;                       // 原文形态（已小写，如 "running"）
    std::string lemma;                         // 词根（"run"）
    CandidateState state = CandidateState::New; // 读者的既有判断
};

// 纯管道，无 I/O。known-set 与词频阈值注入，保持本模块无依赖、可单测。
// 返回按出现顺序去重后的候选，状态随行；空 = 整段不含词表词。
std::vector<Candidate> filterWords(
    std::string_view text,
    const std::unordered_set<std::string>& knownLemmas,  // 命中即标 Known
    std::size_t minFreqRank);   // 档位 → 词表词频阈值（词书数据未就绪时的近似，见 TODO.md）；命中即标 Mastered

// 选区是什么，决定它走哪条通道。实体判定：完整的多 token 名称短语（Title Case 或全大写缩写），
// 或不在词表里的单个字母 token。
enum class SelectionKind {
    Word,      // 整个选区是单个 letters-only token 且在词表：查词通道
    Entity,    // 名称短语，或不在词表的单个字母 token：entity 通道
    Sentence,  // 其余多 token：整段翻译 / 解释
};

struct Selection {
    SelectionKind kind = SelectionKind::Sentence;
    std::vector<Candidate> candidates;   // 非 Word 时为空
};

// 类型判定的唯一入口。出参同时带着候选，免得再跑一遍 filterWords。按钮不参与判定：
// 翻译与解释是同一决策的两个入口（`UI.md` §4.10），句子只在这里记录选区类型。
Selection classifySelection(
    std::string_view text,
    const std::unordered_set<std::string>& knownLemmas,
    std::size_t minFreqRank);

}
```

判定次序：先识别完整的多 token 名称短语（Title Case 或全大写缩写，至少两个 name-like token）→ `Entity`；否则看单个 letters-only token（≥2 字母、不分大小写，如 `QML` / `qml` / `am`）：在静态词表内 → `Word`，不在 → `Entity`；多 token → `Sentence`。`Word` 的候选由 `filterWords` 产出：分词硬过滤（含元音、长度 ≥3、无数字、去 URL / 邮箱 / 粘连串）→ 全大写跳过 → 词根还原 → 静态词表白名单（不在表即丢）→ 按 lemma 去重 → 标状态（New / Known / Mastered）；单个 letters-only token 不走这三道门，见下条。

**通道由选区形状定，不由是否含词表词定**（2026-10-04）：原先 “有候选 = Word” 会把任何含词表词的英文句子送进 word 通道，动作条只解释其中一个词，句子的翻译 / 解释形同虚设。现改为：整个选区是一个字母 token 且在词表 → `Word`（查一个词）；一个字母 token 但不在词表，或多 token 名称短语（Title Case 或全大写缩写）→ `Entity`（命名实体）；其余多 token → `Sentence`（整段翻译 / 解释）。语料里 500 条多 token 条目由 `Word` 改为 `Sentence`（`expect` 候选列表保留，仍由 `filterWords` 断言），`check-eval-corpus.py` 的 `expectKind` 与 `expect` 解耦。取舍见 `docs/metrics/code-quality/selection-classification-accuracy-2026-10-04.md` §9。

**known-set 与档位阈值只标注，不删候选**（2026-10-04）：它们是候选的状态，不是候选的删除条件——读者手选一个词就是在要求解释它，而他可能早忘了自己标过的词。`SelectionKind::Word` 是**选区的形状**（单个字母 token），与 “这段里有没有词表词” 无关；候选只在这条形状下产出并带 New / Known / Mastered 状态，调用方按状态挑词。

**不在词表的孤立 token 是名字，走实体通道**（2026-10-04）：读者把整个选区选成一个 letters-only token 时，若它在静态词表内（`resilience` / `the` / `RUNNING`）就是 `Word`（查词）；不在词表（`QML` / `qml` / `Kubernetes`）就是 `Entity`（命名实体，百科式说明，**无 known / new**）。`filterWords` 的孤立 token 例外照旧产出候选——`surface` / `lemma` 取小写，经 `lemmatize` 后 `RUNNING` 仍还原为 `run`——供 `Word` 用；`classifySelection` 再看 `inTable(surface)` 或 `inTable(lemmatize(surface))` 决定通道，未命中即清空候选转 `Entity`。含数字或内部标点的 token 仍被排除（`MP3`、缩写号、两词）；连续正文里的同类 run 仍由常规过滤处理：`The API returns …` 里 `API` 不是候选。响应侧 `ipa` 可选（缩写 / 标识符没有音标）。语料按此把单 token 条目分成 516 个 `Word` 与 86 个 `Entity`，`check-eval-corpus.py` 的 kind 规则同步。

落地注记（2026-10-02，与测试样例集一并定）：

- **词表是模块内加载一次的只读状态**，不占 `filterWords` 参数位：契约里它本就是唯一权威判定，词根还原也要靠它挑词干。故新增 `loadWordlist`；未加载即调用 `filterWords` / `lemmatize` 抛 `std::logic_error`——这是初始化次序 bug，必须响亮而非静默丢弃全部候选。
- **词根还原 = 候选生成 + 词表择一**，不是无脑剥后缀。不规则表命中即裁定；其余由后缀规则产出 “在词表内” 的词干，与原形并列，取词频序最小者。依据是实测：词表同时收录变形词与原形（running 555 / run 314，increasing 7246 / increase 3568，studied 3632 / study 1273），比词频序天然挑中原形；而 water / under / offer 这类词干不是词的原形会安全落回自身。不规则表必须直接返回而非参与比较——children（451）比 child（461）更靠前，按词频挑会挑回 children 自己。
- **不规则表是数据，不是代码**（2026-10-02 改）：`data/irregulars.tsv` 由 `scripts/gen_irregulars.py` 从 WordNet 的四张屈折异常表（`verb/noun/adj/adv.exc`）生成，5761 条，加载走新增的 `loadIrregulars()`——与 `loadWordlist()` 并列，两者都必须先跑，缺一个即抛。此前是 `filter_core.cpp` 里手打的 74 条，覆盖不到 `criteria → criterion`、`cacti → cactus`、`abaci → abacus` 这类后缀规则根本推不出的形式。源表缺 `women → woman`、`people → person`（WordNet 把这两个复数当成独立词条），由生成脚本里一段带注释的补漏补齐。
- **一词多 base 的归属**：表里极少数形式有多个原形（`better` → good / well），本地无从分辨义项，按词表词频择一；单 base 的形式仍命中即裁定。
- **有意的近似**（`ponytail:`，样例集撞出误判再放宽）：`-er` 仅 ≥6 字母、`-est` 仅 ≥7 字母启用，故 nicer（5 字母）与 nicest（6 字母）不还原——**注意 biggest 是 7 字母，规则会触发并经叠辅音减一还原成 big**（早先此处写成 “biggest → big 不还原” 是错的，与代码不符，2026-10-02 更正）；`-ing` / `-ed` 仅 ≥5 字母启用，避开 seed → see 这类伪还原；`news` / `means` 列入 keepAsIs 例外表——它们不在异常表里，需要挡掉 `-s` 规则。
- **分词**：按空白切段，削去首尾既非字母也非数字的字节，剩余内部只要还有非字母字节，整段判粘连串丢弃。数字留在 token 内而不是削掉——否则 version2 会被削成 version 反被弹出。
- **含元音**判定把 `y` 计入，救回 rhythm / myth / gym。
- 去重按 **lemma**（非 surface）：同段内 run 与 running 只留首次出现。

落地注记（2026-10-04，与样例集扩容一并定）：

- **状态进候选，删除逻辑出模块**：`Candidate` 加 `state`。known-set 命中记 `Known`，`rankOf(lemma) <= minFreqRank` 记 `Mastered`，两者都命中时 Known 优先——读者的明示标记比档位推断更具体。`filterWords` 不再因这两者丢候选，`classifySelection` 的 kind 因此只看 “这段有没有词表词”。阶段二的自动扫描才是需要整体跳过这两类状态的调用方，本阶段没有调用方需要这个跳过，所以调用方侧不加过滤。
- **选区发首个 `New`，一个都没有时发首个候选**：`AppController::beginSelection` 按状态挑词。不挑的话，句首冠词会变成请求词（`The ubiquitous …` 会去解释 the）。整段都被标过、或都落在档位带内时首个候选顶上——读者手选的词照样解释，这正是本次要修的那条。
- **气泡状态与通知路径**：`showBubble` 的 status 一直由 `store_.isKnown()` 现算，所以已知词的气泡现在一上来就带 “已知” chip，而不是等到一次判定之后。`showNotice` 的 status 仍是空串，QML 的反馈按钮以 `status !== ""` 为前提，因此通知的标题（选区原文，不是词根）不会写进已知词库。

### 4.2 KnownStore（JSON 持久化，无 Qt）

```cpp
namespace lens::core {

struct WordCache { std::string ipa, en, zh; };

class KnownStore {
public:
    static KnownStore load(std::filesystem::path path);   // %APPDATA%\Lens\settings.json
    bool isKnown(const std::string& lemma) const;
    void mark(const std::string& lemma, bool learned);     // 已会 / 新词
    const std::unordered_set<std::string>& known() const;  // 喂 FilterCore::filterWords
    int level() const;  void setLevel(int);                // 0..7，取值见 GLOSSARY.md「档位」
    std::string explanationLang() const;  void setExplanationLang(std::string);  // "en"/"zh"
    std::optional<WordCache> cacheGet(const std::string& lemma) const;
    void cachePut(const std::string& lemma, WordCache);
    void save() const;
};

}
```

全模块用 `std::string`，JSON 解析走 `third_party/nlohmann/json.hpp`（header-only），因此 `lens_core` 对 Qt 零依赖，可直接被 `lens_test` 离线链接。

单 JSON 文档，加载一次、变更即存。known-set 与缓存均小，暂不上 SQLite（`ponytail:` 到量再迁）。文件同时承载 LLM API 配置（密钥），归属隐私边界（见第 6 节）。

存储形状、写入规则与隐私边界（共用文档、只覆写自己的键）见 `PRODUCT.md`“存储形状”。这里只留接口本身的约定：

- `setLevel` 越界抛 `std::out_of_range`；`load` 见越界值回落默认档。
- 档位序号到词频阈值（`filterWords` 的 `minFreqRank`）的映射尚未落地，属 AppController 切片，配合 `TODO.md` 词书数据一起做。
- **缓存条目必须带 `ipa` 字段，但可为空**（2026-10-04 加 `ipa`，同日改为可选）：音标是 word 气泡的一格，但缩写 / 标识符没有音标，故字段可在而值为空。`load` 丢掉的是**没有 `ipa` 键**的更早记录（旧文档里的老记录），下一次查同一个词就是未命中，重新问模型并写回完整的一条——未命中就是迁移，没有单独的迁移代码。

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

统计三个弹窗（统计 / 词汇 / 花费）的数据层就是上表，此前没有归属，切片三补上（2026-10-03）。它与
`KnownStore` 共用 settings.json 的规则、`history` 的上限、token 与金额的分工，见
`PRODUCT.md`“存储形状”。

### 4.3 LlmClient（QObject，异步）

```cpp
namespace lens::llm {

struct Config { QUrl baseUrl; QString apiKey; QString model; };
struct Explanation { QString title, ipa, en, zh; }; // word：word/ipa/en/zh；entity / sentence：en/zh，title 由 App 盖上
struct Usage { int promptTokens = 0; int completionTokens = 0; };  // 响应 usage 字段

class LlmClient : public QObject {
    Q_OBJECT
public:
    explicit LlmClient(Config, QNetworkAccessManager* manager = nullptr, QObject* parent = nullptr);
signals:
    void batchFinished(QVector<Explanation> results, Usage usage);  // 校验通过
    void failed(QString message);                                       // 网络 / schema 失败
public slots:
    void explainWords(QStringList words);                               // 一次 HTTP，批量
};

}
```

契约要点：

- 发送前脱敏（邮箱 / 长数字 / URL 掩码）；单词已过硬过滤，此处为兜底。
- **响应是不可信数据**：按 schema 校验；单词通道另做输入词逐一回显核对，缺失或多余 → 整体失败，不静默丢词。实体 / 句子不回显，按请求顺序取结果；模型把一段拆成多条（如带编号的列表）时按顺序合并成一条。
- 一次请求上限 20 词；超出静默截断（当前调用方单次仅 1 词，实际不触达）。
- `setExplanationLang(QString)` 为切片二新增，契约原表未列：解释语言在设置浮层里运行时可变，塞进构造期的 `Config` 不合适；它只切 §5 提示词末句。
- 纯函数内核与传输层分离：`src/llm/llm_pure.{h,cpp}` 放脱敏、请求体构造、响应校验三个无网络函数（`lens_gtest_unit` 覆盖），`llm_client.{h,cpp}` 只剩 QObject + `QNetworkAccessManager`。

落地注记（2026-10-02，DeepSeek 官方文档核验）：端点与 `baseUrl`、模型名与停服旧名、必关思考
（`thinking: disabled`）与 `temperature` / `top_p` 的取值、`max_tokens`、`finish_reason` 必查、错误码归类、
usage 的容错、价目与展示币种——全部见 `API.md`（开头、§2、§5、§6 各占一段）。

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
    Q_INVOKABLE void setClipboardPolicy(QString policy); // "topmost"（缺省）/ "silent"，见抓取那条
    Q_INVOKABLE void setAutostart(bool on);              // HKCU 的 Run 项，见 autostart.h

    Q_INVOKABLE void bubbleHoverChanged(bool hovering);  // QML 持有 5 秒计时，这里只记状态
    Q_INVOKABLE void dismissBubble();                    // 关掉当前解释
    Q_INVOKABLE void dismissNotice();                    // 只关掉当前通知，不影响解释或动作条

    Q_INVOKABLE QString exportWords(QString scope);      // "all" / "known" / "new"，纯文本一行一个词根；不选路径也不写文件
    Q_INVOKABLE bool saveWords(QUrl path, QString scope); // 把上一行那份文本写进 path（对话框选的文件），LF 结尾

    Q_PROPERTY(QVariantMap bubble READ bubble NOTIFY bubbleChanged)      // 当前解释，空 map = 无气泡
    Q_PROPERTY(QVariantMap notice READ notice NOTIFY noticeChanged)      // {title, body, kind}，空 map = 无通知
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)         // 今日计数 + 历史累计
    Q_PROPERTY(QVariantList words READ words NOTIFY statsChanged)        // 词汇弹窗的行（含弹词次数 pops）
    Q_PROPERTY(QVariantMap cost READ cost NOTIFY statsChanged)           // 花费弹窗的各档金额与 token 数
    Q_PROPERTY(QString modeLabel READ modeLabel NOTIFY settingsChanged)  // 托盘首行与 tooltip 共用
    Q_PROPERTY(QString busyLabel READ busyLabel NOTIFY busyChanged)      // 托盘 icon 的「解释中」态

signals:
    void selectionBarRequested(QVariantMap payload);      // → QML 弹选区动作条：{x, y, kind, text}
    void bubbleChanged();
    void noticeChanged();
    void settingsChanged();
    void statsChanged();
    void busyChanged();                                   // 托盘 icon 切「解释中」
    void uiLanguageChanged(QString lang);                 // → 启动处换 QTranslator
};

}
```

落地注记（2026-10-04，接缝先行）：

- **通知是它自己的一张卡片，不是气泡的一格**：请求失败原先往气泡里塞一段文字，气泡因此兼任错误播报。
  现在请求失败走 `notice`——`{title, body, kind}`（`src/app/notice.h`），不带 status chip，因为什么都没被
  解释；气泡重新只装解释。`kind` 有 `info` 与 `error` 两个值，当前生产路径（网络、密钥、schema 三类请求
  失败）都走 `error`；无可用通道在三通道落地后已不存在，选区抓取失败改为静默丢弃（见下条），`info` 暂无路径。
  渲染方式留给表面。清空时机：弹出新解释时、`dismissNotice()` 时、以及被下一条通知替换时。通知**不带锚点**
  ——它不属于某一次选区，落点由表面自己定。解释的 `dismissBubble()` 只清解释，避免旧窗口的关闭回调清掉当前通知。
- **剪贴板被别的进程抢走时不再写回**：抓取期间剪贴板若被改写（剪贴板管理器、或读者自己复制了别的），
  把快照写回会毁掉对方刚放下的内容，于是快照留在原地，`GrabbedText::clipboardReplaced` 记下这件事。
  **读到的文本仍然有效**——它是在第二次写入之前读出来的——所以这不是一次失败的抓取，缺的只是读者的
  剪贴板（那一刻已经丢了，救不回来）。`clipboardPolicy` 因此只回答一个问题：这次还要不要弹。`topmost`
  （缺省）照常弹，表面显示时本来就会 `raise()`（`docs/QML.md` §2），置顶正是这个值的意思；`silent`
  这一次什么都不弹，读者自己再选一次即可；快照无法取得、前台是终端或无法注入复制时同样**静默丢弃**，
  不弹通知（2026-10-04 起）。
- **通知路径的覆盖边界**：网络失败、schema 校验失败、缺少 API key 到达通知卡片；无可用通道那条路径被三通道
  取代（每次选区都有通道）；选区抓取失败（自身表面 / 终端 / 剪贴板占用 / 无法注入 / 无文本）一律静默丢弃，
  不弹通知（弹窗太扰人）。每日预算上限仍是阶段二占位项，当前没有生产事件，也不以 fixture 冒充已覆盖。
- **选区封顶与迟到响应**（2026-10-04）：选区超过 1000 字符只取前 1000 加省略号（整篇误选不至于烧 token、
  卡请求、把巨大标题画到卡片上）。模型调用期间若读者又选了一段，`pending_` 被覆盖，旧请求的响应到达时与
  `pending_` 不匹配即丢弃（按 `title` 判断：word 比 lemma 大小写不敏感，entity / sentence 比原文），否则
  会把上一段的释义画到这一段的卡片上、并把词缓存写到错的 lemma 上。
- **开机自启**：`settings()` 多一个 `autostart`，写入 HKCU 的 Run 项（`src/app/autostart.{h,cpp}`）。键名与
  命令行由纯函数拼装，可离线断言；真正的注册表写入是机器状态，属人工用例。
- **词汇可导出**：`exportWords(scope)` 返回纯文本（一行一个词根，与 `data/wordlist.txt` 同形），不选路径、
  不写文件；`saveWords(path, scope)` 才落盘，因为 QML 的文件对话框只能选文件、写不了。三个 scope 就是词汇弹窗
  的三个筛选，行集与 `words()` 一致。词汇弹窗
  的行多带一个 `pops`（该词被弹过几次），由历史在展示时数出，存储形状不动；历史留 2000 条（§4.2），
  次数随之封顶。
- **单词行带音标**：单词响应 schema、`Explanation`（§4.3）、气泡载荷与缓存（`WordCache`，§4.2）都加了
  `ipa`，原先 “阶段一不放音标位” 那条随之作废。四处的顺序一致（`word` / `ipa` / `en` / `zh`），气泡的
  `bubble` 映射里 `ipa` 排在 `title` 之后、两种释义之前；它不是释义，所以不随 “解释语言” 切换而清空——
  命不命中缓存都拿得到。存储形状的这一处改动记在 `PRODUCT.md`“存储形状”，缺 `ipa` 键的旧缓存按未命中处理。

落地注记（2026-10-03）：

- **接口形状**：`bubble` 与 `settings` 都是 `QVariantMap`，不是 QObject 模型——表面数量个位数、字段都是标量，为每个表面写一个 `QAbstractItemModel` 是给 QML 添一层没人问的间接。`modeLabel` 一个属性喂托盘菜单首行与 tooltip 两处（`UI.md` §4.2 / §4.5），分两处拼字符串必然漂移。`cost` 的五个数在 C++ 算：本月 / 今天 / 昨天 / 本周 / 日均的日期运算用 `QDate`，core 侧只存 token 与按日计数（§4.2）。
- **5 秒计时归 QML**：自动消失与悬停挂起是视图行为（`UI.md` §4.3），计时的持有者在 QML；`bubbleHoverChanged` 只让 C++ 知道状态，不参与计时。否则计时器要跨进程边界地和悬停事件对齐。
- **类型判定从 `beginSelection` 里提出来**：判定原本是行内一句 `candidates.empty()`，现收到 `core::classifySelection`（§4.1），出参是 `SelectionKind` 加候选。实体、单词、句子的路由在同一处决定，按钮不参与判定；`lens_gtest_unit` 可直接覆盖这条无 Qt 规则。`beginSelection` 按 `switch (kind)` 取值而非再看一次 `empty()`——枚举穷尽时编译器会在加通道那一刻报错。
- **三通道的气泡边界**：word 解释保留 `ipa`、单词缓存、已会 / 新词 verdict 与词汇统计；entity / sentence 不显示 IPA / verdict，也不写入单词缓存与弹词历史。实体气泡画名字 + 百科式释义、**不带类型标签**（命名实体不是学习词条，没有 known / new）；句子气泡只画 “句子” 描边标签 + 译文 / 讲解，**不画原句**（长句会溢出卡片）。实体的两个动作共用 `default` 预设，句子的 `translate` / `explain` 分别选择同名预设。（三通道落地后每条选区都有通道，气泡不再兼职播报失败；请求失败走 `notice`、抓取失败静默丢弃，见上一条。）

落地注记（2026-10-02）：

- **触发是选区完成，不是剪贴板变化**：Windows 没有 API 能直接读到别的应用里被选中的文字，所以入口定为低层鼠标钩子（`WH_MOUSE_LL`）：`LBUTTONUP` 且按下期间确实拖动过（双击选词、三击选段同理）即算选区完成，回调 `onSelectionReleased(anchor)`，锚点就是松手坐标，不必再拿 `QCursor::pos()` 近似。取文靠 `SendInput` 向当前前台应用注入 Ctrl+C 再读剪贴板——覆盖最广的一条路，凡能复制的应用都通（含 PDF 阅读器）。
- **两个必须处理的副作用**：一是剪贴板被顶掉——注入前存、读完还原，其间用户恰好复制的东西会被吞（`ponytail:` 竞争窗口，真被投诉再上 UIA TextPattern 绕开剪贴板取文；2026-10-04 起，窗口内被别的进程写入的剪贴板会被认出来，快照不再写回，见上面那条注记）；二是注入的 Ctrl+C 在终端里就是 SIGINT——按前台进程名排除终端类（Windows Terminal / conhost / PowerShell），与 `PRODUCT.md` 的扫描白名单同源。
- **注入前必须确认前台不是自己**：靠 `WindowDoesNotAcceptFocus`——动作条与气泡都不夺焦点，点它们不会污染下一次注入的目标。**钩子也必须认自己的窗口**：按下落在本进程的窗口上时，这次按下既不是选区手势，也不算双击的第一击（`WindowFromPoint` + `GetWindowThreadProcessId` 比对 PID），否则在面板上拖动会被当成拖选、松开时注入 Ctrl+C 把背后应用里的选中内容弹出来。代价写在函数注释里：透明的阴影边距也算自己的窗口，贴着面板 26 px 内起手的真选区会被放掉。
- **动作条介入数据流**：`onSelectionReleased` 不再直接通向气泡，中间隔着选区动作条。回传的 `action` 取 `translate` / `explain` / `copy`：`copy` 本地结束；word / entity 的 `translate` 与 `explain` 行为相同（通道由类型定），只有 sentence 用 `action` 选 `translate` / `explain` 两个预设（2026-10-04 更新）。类型判定在动作**之前**发生，结果放进 `selectionBarRequested` 的 `kind`，QML 不参与判定。
- **捕获组件是两个文件**：`mouse_selection_hook.{h,cpp}`（低层钩子与手势规则）与 `selection_text_grabber.{h,cpp}`（注入 Ctrl+C、剪贴板存还原）。终端排除与前台自查都落在取文那一步——危险发生在注入处，那也才是查得到前台进程的地方；`selectionReleased` 只带 `QPoint`，不带进程名。两条纯谓词 `isSelectionGesture` / `isExcludedProcess` 收普通参数，可离线断言。
- **锚点从钩子到表面要先换算**：钩子拿到的坐标是物理像素，而 QML 窗口落在设备无关像素上，两者差一个 `devicePixelRatio`（本机 125% 实测：物理 x=275 的松手位置，窗口坐标是 220）；按 1:1 直接用，动作条会偏出选区四分之一屏。换算必须在交出之前做完，`Main.qml` 的 `toDip()` 是唯一换算点；它自己的实现坑（`QVariantMap` 属性赋回自己无效，得造新对象）见 `docs/QML.md` §4。

窗口与表面的落地形态、阴影、定位、拖动、跨线程、字体的实现约束，一律见 `docs/QML.md`。

QML 表面：设置浮层、选区动作条、解释气泡、统计弹窗及其下钻的词汇 / 花费弹窗，按 `UI.md` 规格落地；占位项 `enabled: false`。托盘图标（四状态）用 **C++ `QSystemTrayIcon`**，菜单是一个普通 QML 表面——两者的取舍见 `docs/QML.md` §7。

### 4.5 Test（`test/googletest/`，独立目标，不编进 `lens_app`）

测试代码与样例集是单独的可执行目标，只在本机构建，**不进发布包**：它是 `lens_core`（无 Qt）→ `lens_llm` → `lens_app` 这条链之外的旁支，`lens_gtest_unit` / `lens_gtest_smoke` 链接 `lens_core` + `lens_llm`，`lens_gtest_perf` 只链接 `lens_core`（保持无 Qt），`lens_gtest_integration` 链接 `lens_app` + `lens_core`。

契约要点只有一条：**样例集 `test/eval_corpus.json` 是 FilterCore 的行为规格**——改行为先改样例集（`GLOSSARY.md`“自检样例集”），测试红了再动 `src/`。离线那一支零网络、零密钥，**可进 CI**。

框架、目录、三个目标、样例集字段表与运行方式见 `TEST.md`。

### 4.6 Profiling（`src/core/profile.{h,cpp}`，零 Qt）

`lens_core` 内的测量点，由 CMake 选项 `LENS_ENABLE_PROFILE` 做编译期开关。三条契约约束：

- **零 Qt**，与 §4.2 一致。
- **不逐条打日志**。热路径按调用记日志会淹掉日志本身，并盖住要观察的现象；站点只累加，热阶段结束后用一次 `report()` 出汇总表。
- **计时器不是免费的**，所以两个原语并存：作用域计时器给粗粒度，计数器给每 token 量分母。先用计数器，再决定要不要给内层计时。

原语定义、开销实测、profile 构建树与记录约定见 `TEST.md` §4。

## 5 LLM Prompt 与 JSON Schema

线上格式的完整说明（请求体、响应体、校验规则、错误码）见 **`API.md`**；这里只记契约要点。

目标模型：DeepSeek（OpenAI 兼容）。**提示词与 schema 都是数据，不是代码**——三通道的请求与响应文件在 `data/llm/`，句子请求文件包含 `translate` / `explain` 两个预设，改提示词或加字段是改数据，不用重编译。

落地注记（2026-10-02，与 V3 一并定）：按通道拆分文件、系统提示词用英文且只切末句、响应 schema 是校验
的唯一真源、`llm_protocol` 加载失败一律抛而不回落默认值——见 `API.md`。word schema 是 `word / ipa / en / zh`（
`ipa` 可选，见 §4.1），entity 与 sentence schema 是 `en / zh`（不回显 `title`，App 把选中文本盖上——
模型逐字复现长串 / 带标点 / 指令式文本会漂移，2026-10-04 改），IPA 只属于单词。句子请求的两预设任务随
解释语言改变：中文时 `translate`=字面中文翻译、`explain`=中文通俗解释；英文时两者都=英文通俗解释（提示词
相同），气泡只画所选语言那一行。

## 6 隐私与密钥边界

- API key **仅存本地** `%APPDATA%\Lens\settings.json`（`main.cpp` 的 `settingsPath()`），不提交、不入日志、不写入本会话记忆。输入经设置浮层（掩码显示）或配置文件。仓库根的 `settings.local.json`（gitignored）是这份文档的开发来源，首次运行时整份拷进 `%APPDATA%` 一次，此后不再读它；安装目录里**不放**这份文档，它可能在 `Program Files` 下只读，且是全机共用的。
- 发送最小化：单词通道只发单词本身，脱敏兜底；绝不发送整屏或快照全文。
- **档位与界面语言不进请求**：档位只影响本地词表与 known-set 预置（见 `GLOSSARY.md` 档位条目）。2026-10-02 的系统提示词曾写进 “CET-4 level or above”，属违规，已删；提示词只说取最常见义项，判断全在本地。
- 注意：本次会话中曾粘贴真实 key（已进对话记录），建议开发完成后轮换。

## 7 目录结构与构建目标

```
lens/
├── CMakeLists.txt
├── CMakePresets.json         # preset ninja-qt6（首选）+ vs-qt6（备选）
├── .clang-format
├── settings.local.json       # 开发机的密钥与设置，gitignored；首次运行拷进 %APPDATA%
├── installer/                # lens.iss（Inno Setup 安装包）与 CMake 生成的 version.iss
├── scripts/build.bat         # 进 VS 环境后驱动 ninja（首配一次，之后纯增量）
├── API.md                    # LLM 线上格式说明（请求 / 响应 / 校验 / 错误码）
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

现状：`lens_core` 为 STATIC（`log.cpp` + `profile.cpp` + `filter_core.cpp` + `known_store.cpp` + `stats_store.cpp`），`lens_llm` 亦为 STATIC（`llm_pure.cpp` + `llm_client.cpp` + `llm_protocol.cpp` + `llm_pricing.cpp`），`lens_gtest_unit` / `lens_gtest_smoke` 链接两者，`lens_gtest_perf` 只链接 `lens_core`。`src/app` 为 STATIC，装着捕获组件（§4.4）、`AppController` 与托盘图标（`QSystemTrayIcon`；菜单是 QML 表面），除 `Qt6::Core` 外挂 Gui / Widgets / Quick 与 `user32`。

**QML 模块挂在 `lens_app` 上，不在可执行文件上**（2026-10-03 改）。`qt_add_qml_module` 只注册它自己那个 target 的 C++ 类型，而 `AppController` / `Tray` 住在静态库里：模块挂在 `lens` 上时一个类型也注册不了，QML 只能靠 context property 拿到它们，于是那两个名字对 lint 隐形（53 条警告），属性名写错也只是运行时读成 undefined。代价有两条：静态库的 QML 插件要显式链进可执行文件（`target_link_libraries(lens PRIVATE lens_appplugin)`——Qt 把 `Q_IMPORT_PLUGIN` 那个 init 对象挂在该插件 target 上，少了这一行表面照常加载，但每个 `Controller` / `Tray` 引用都是 ReferenceError），以及 `lens_gtest_integration` 链接 `lens_app`、从此也被带上 Qt Quick。取舍与取舍的账见 `docs/adr/0004`。

## 8 预检清单（动工前）

| 项                  | 状态                                                                                                                                                                                                                                                                  |
| ------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Qt 6 + CMake 工具链 | **已完成**：Qt 6.9.0 MSVC2022_64 @ `B:/qtt/6.9.0/msvc2022_64`，preset `ninja-qt6`（首选）/ `vs-qt6`（备选）                                                                                                                                               |
| 静态词表            | **已完成**：`data/wordlist.txt` 已落盘                                                                                                                                                                                                                        |
| 密钥注入            | **已完成**：密钥与设置在 `%APPDATA%\Lens\settings.json`（2026-10-05 起），开发机的 `settings.local.json` 已 gitignore                                                                                                                                                                                                    |
| DeepSeek API 核验   | **已完成**（2026-10-02）：模型名 `deepseek-flash` / `deepseek-v4-pro`；`response_format: {"type":"json_object"}` 支持，且要求 prompt 含 `json` 字样与格式示例；OpenAI 格式端点为 `{baseUrl}/chat/completions`。结论与落地细节见 `API.md` §6。 |

预检四项已全部完成，可以动工。核验渠道说明：本机未装 `uv` / `mcp` 包、也没有 `CONTEXT7_API_KEY`，`context7` 不可用；改直接抓 DeepSeek 官方文档（`api-docs.deepseek.com`，国内直连可达，且是更权威的一手来源）。

## 9 验证

- FilterCore 单测：样例集离线断言（`lens_gtest_unit`，零网络，可进 CI）。**已通过**：样例集 3005 段 + 存储往返，覆盖档位词频阈值、词根还原（后缀规则与 WordNet 异常表）、只有表能覆盖的形式（`criteria → criterion` 一类）、垃圾内容（URL / 邮箱 / 文件名 / 带数字串 / 连字 / 全大写）、不在词表即丢、同段同词根去重、已知与已掌握只标注不删。样例集另有 `scripts/check-eval-corpus.py` 按规则独立校验，与实现无关。
- 单层判定准确率：**已测量**（2026-10-04，三轮，`lens_gtest_unit` 回放样例集）：1001 段中 336 段未命中，命中率 66.43%，三轮同值。未命中全部落在 “已知 / 已掌握被当作删除条件” 这一条轴上（带 minFreqRank 的 173 段错 172，带 known 的 163 段错 163，两者都不带的 664 段全对）——没有一段是层级判定问题，故不引入多层级策略。报告：`docs/metrics/code-quality/selection-classification-accuracy-2026-10-04.md`。
- LLM 纯函数接缝：脱敏 / 请求体 / 响应校验三项离线断言（`lens_gtest_unit`）。**已通过**：覆盖邮箱、URL、长数字掩码；`model` / `stream:false` / `response_format` / `thinking:disabled` / `max_tokens` / 提示词含 `json` 与格式示例 / 解释语言切末句；`finish_reason != stop`、外层非 JSON、缺 `results`、字段缺失、回显错词 / 漏词 / 多余词一律整体失败。断言经变异验证确实会红。
- 迁移核对：**已通过**（2026-10-02）：手写 `CHECK` 转入 gtest 后为 11 例（样例集 1 + KnownStore 6 + 掩码 1 + LLM 纯函数 3），`lens_gtest_unit` 全绿。原 `main.cpp`（452 行）删除。
- LLM 冒烟：**已通过**（2026-10-02，迁移前的手写版本）：`ubiquitous` 经 `deepseek-flash` 真实往返，`word` / `en` / `zh` 三字段回显与 §5 schema 一致，`finish_reason=stop`。迁移后的 `lens_gtest_smoke` 尚未跑过真实往返，待人工执行。
- FilterCore 热点：**已测量**（2026-10-02，RelWithDebInfo，`LENS_ENABLE_PROFILE=ON`，样例集 8 段 × 200 轮，`filterWords` 1600 次 / `lemmatize` 8400 次 / token 10800 个）。**`lemmatize` 只占一部分，其余落在分词与硬过滤阶段**，比此前 “热路径即 lemmatize” 的说法更宽。五轮数据（见 `TEST.md` 的记录约定）：`lemmatize` 很稳，1.85–2.04 ms（221–240 ns/次）；`filterWords` 在 4.36–7.04 ms 之间摆，占机器干扰最大的分词侧。因此占比是区间而非点值：**最干净的三轮约 43%，受干扰时降到 27%**。同株关掉插桩作对照，插桩整体抬高约 16%，反推一次计时器约 60 ns；扣掉后下限约 36%。样例集扩到 1001 段后（2026-10-04）同法重测，七轮：`filterWords` 每轮中位 32.60–42.99 ms（min 31.36–31.76），每段 0.0314–0.0430 ms。工作量变了，与上面 8 段时的绝对值不可比，只有每段的口径可比；七轮的中位摆到 10 ms 也再次说明这台机器的噪声，读这个数要连 min 一起读。
- 一次运行的定量结论都带这类区间，跑 `lens_gtest_perf` 时至少看三轮。
- 选区捕获（2026-10-03；2026-10-05 订正）：`lens_gtest_integration` 15 例，默认跑法 14 例通过、1 例跳过（跳过的要人手拖鼠标，`LENS_HOOK_SMOKE=1` 才放开）。当日实际是 13 例通过、1 例跳过、1 例红——合成拖拽那条的落点窗口属于本进程，被钩子 “自己窗口不取” 的规则挡下，不是此前以为的钩子线程进消息循环的时序；2026-10-05 改为让落点窗口由子进程持有后转绿（W2）。同日另补两条不依赖人手的抓取器分支用例，现为 **17 例**（默认 16 例通过、1 例跳过；取不到前台窗口时 15 通过、2 跳过，两种都不是红），逐条见 `TEST.md` §2。机器已验证——手势规则（含阈值边界、双击 / 三击、反向拖动）、终端排除名单的大小写与全路径匹配、钩子装上后能收到拖拽并按松手坐标发出锚点（拖拽由 `SendInput` 合成，低层钩子对合成事件与真实事件一视同仁）、前台是自己时取文拒绝执行，以及剪贴板存还原的三面（内容被顶掉后还原、非文本格式一并还原、没取过快照时不许动剪贴板）。真人手测过一次（2026-10-03，Windows 记事本 11.2607 商店版）：注入的 Ctrl+C 确实取到了选区，十字相符——契约里那句 “凡能复制的应用都通” 有实证了。同一次的日志还给出了逐格式拷贝的量化理由：记事本一次 Ctrl+C 往剪贴板放了 4 种格式，快照全数取回并全数还原，**只存文本会毁掉其中 3 种**。
- **剪贴板的存还原用 OLE 是错的**（2026-10-03 实测推翻）：`OleGetClipboard` 取出的 `IDataObject` 交给 `OleSetClipboard` 一律失败，`CLIPBRD_E_CANT_CLOSE` 或 `CLIPBRD_E_CANT_OPEN`；中间有没有变化、内容由本进程还是别的进程（`clip.exe` 验过）放入，结果都一样，而同一次运行里 `OleSetClipboard(nullptr)` 却成功，所以坏的是对象往返而非 setter。现在的做法是裸开剪贴板逐格式读出字节，还原时空盘再逐格式写回；位图 / 调色板 / 增强图元文件 / owner-display 族这类句柄格式按名跳过并记日志；`ole32` 不再需要。这条错误原先只有人工用例能碰，改成三个离线用例后当场复现。
- 全链手测：复制真实英文句 → 浮层弹词 → [已会]/[新词] 回写 → 复弹不重复。
- V3 离线验证（2026-10-04）：`lens_gtest_unit` 43 例全绿；语料 1004 段回放 `0 mismatched`，独立语料检查 `0 problem(s)`；两条 sentence preset 与 entity 两字段响应各有纯函数断言，并有 loopback mocked HTTP 往返。
- V3 表面验证（2026-10-04）：`lens_qtest_surfaces` 43 例通过、1 例按快照目录跳过；`lens_qtest_components` 44 例通过、1 例按快照目录跳过；新增 `tst_channels.qml` 独立覆盖三类动作条与 entity / sentence 气泡。
- V3 的 `lens_gtest_smoke` 仍未执行真实请求，避免未经批准产生费用；人工命令见 `TEST.md` §2，结果待 supervisor / user 批准后记录。
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

这一段不在原计划里，是几次按需请求累积出来的，单独记一笔以免来历不明：spdlog 与 `LENS_*` 日志宏（Qt 消息并入同一 logger）、Ninja 构建与 `scripts/build.bat`、全项目英文 Doxygen 注释、i18n 骨架（`i18n/*.ts`，英文为源语言）、LLM 线上协议数据化（`data/llm/` + `API.md`）、不规则屈折表数据化（`data/irregulars.tsv`）。

第二次追加（2026-10-02，同样不占切片号）：GoogleTest 进 `third_party` 并退役手写自检（§4.5）、`src/core/profile.{h,cpp}` 与 `LENS_ENABLE_PROFILE` 选项（§4.6）。两件都是基建，没有可独立验收的用户交付物，故按上一段的先例记在这里，不另起切片号。

### 切片三：AppController + QML 表面（进行中）

- **范围**：`src/app` 落地。`AppController`（选区入口：鼠标钩子 + Ctrl+C 取文、选区类型判定与锚点、动作条回传、候选查缓存、known-set 回写、设置读写）与 `UI.md` 的全部表面（设置浮层、选区动作条、解释气泡、统计弹窗及其下钻的词汇 / 花费弹窗、托盘菜单）；托盘图标四状态用 **C++ `QSystemTrayIcon`**，菜单是一个表面（见 `docs/QML.md` §7）。
- **含**：§4.2 遗留的 “档位序号 → 词频阈值” 映射，与 `TODO.md` 的词书数据一起做（`minFreqRank` 目前是近似，见 §4.1 落地注记）。
- **含**：i18n 首次真正生效——QML 目录加入 `lupdate` 扫描，`.qm` 经 CMake 构建、启动时加载（现在 `.ts` 只有 llm 模块的 22 条，加载机制尚未接）。
- **含**：统计三个弹窗的数据层 `StatsStore`（§4.2）与金额所需的 `Usage` + 价目（§4.3）——此前两个契约里都没有，是切片三补的。
- **验收**：复制真实英文句 → 浮层弹词 → [已会] / [新词] 回写 → 复弹不重复。“复弹不重复” 指**请求**不重复：已知词不再被删（§4.1），第二次选中同一段仍会走到解释这一步，但 `store_.cacheGet`（lemma + 解释语言）命中就直接弹缓存，一次也不出网；弹词计数照记。占位项 `enabled: false` 不可触发；界面语言中英切换生效。

落地注记（2026-10-03）：

- **选区的类型判定发生在动作之前**：`onSelectionReleased` 走完取文只判定并弹动作条，不请求；点 “翻译” 或 “解释” 之后才按判定结果分流，单词发请求，无候选时只有复制文本这一条路走得通。
- **发出去的是词根，不是原文形态**：选中 running 时，请求、缓存键、气泡标题、历史记录统一用 lemma（run）。§4.2 已定缓存按词根分键，若请求发 surface，同一次选择就会产生 “问的是 running、缓存存的是 run” 两套键，再选 ran 也命中不了。代价是气泡标题显示词典形而非读者选中的词形。

窗口与表面、阴影、定位、拖动、跨线程、字体的实现约束，见 `docs/QML.md`。

### 切片四：阶段二通道（V3 已完成，OCR 仍占位）

- **V3 范围**：选区实体与句子解释。实体使用一个 `default` 预设，句子使用 `translate` / `explain` 两个预设；两类响应均为 `en` / `zh` 两字段（2026-10-04 起不回显 `title`，由 App 盖上），不进入单词缓存与 verdict。实体分类接受：不在词表的单个 letters-only token（`QML` / `Kubernetes`），或至少两个相邻的名称 token（Title Case 或全大写缩写）的短语。
- **剩余范围**：OCR 取词（扫描 / 悬停 / 截图）、误弹反馈入口、每日预算上限仍是占位项，设置中不可触发。
- **交付**：`data/llm/` 的三通道请求与 schema、`llm_protocol` 的 preset loader、`LlmClient` 的 channel / preset 路由、`FilterCore` 的实体分支、`AppController` 的三通道动作路由与独立 QML 通道测试。
