# 阶段二实现规格（1.1.0）

> 目标版本 1.1.0。本文是阶段二的实施契约：范围、每项功能的要点与验收判据。
> 阶段一的契约已归档，本文不重复；术语见 `GLOSSARY.md`，UI 规格见 `UI.md`，
> 线上格式见 `API.md`，测试与运行方式见 `TEST.md`，QML 约束见 `docs/QML.md`。
> 本文只排范围与验收，不复制它们的内容。

## 1 目标与验收

阶段二把取词从 “显式选区” 扩展到 “屏幕 OCR”，把解释从 “单词一条” 扩展到 “多义 + 多语言”，
并补齐存储、主题、模型选择、安装与工程收尾。实施按 §2 的七个阶段与并行波次推进。

**验收版本 1.1.0**：`lens_gtest_unit` / `lens_gtest_perf` / `lens_gtest_integration` 与两个 QTest 目标全绿
（`lens_gtest_smoke` 人工跑）；`scripts/quality/qml-lint.sh` 零警告；每项功能按各自 “验收” 一节有可执行证据；
1.0.0 的已知 bug 全部关闭；安装向导的视觉与软件主题一致。

## 2 实现阶段与并行波次

十三项拆成七个实现阶段（切片），每个阶段一个 worktree 分支独立推进。可并行的前提是 “文件所有权” 不相交：共享枢纽先在波次 0 拆出接缝，其余阶段才互不踩踏。功能细节仍在 §3–§8，本节只排阶段、所有权与波次。

| 阶段 | 分支 | 覆盖条目 | 文件所有权 | 前置 |
| --- | --- | --- | --- | --- |
| F 基础 | `feature/phase2-foundation` | §5.1、§7.1、§7.2 | 新 `src/util/**`、根与各 `CMakeLists.txt`、`llm_client.*`、`app_controller` 接缝、`SettingsPopup` 分类拆分、`docs/adr/0012` | 无 |
| A 取词 | `feature/phase2-capture` | §3.1、§3.2 | 新 `src/capture/**`、`mouse_selection_hook.*`、取词设置 section 与 duty class | F |
| B 解释 | `feature/phase2-explanation` | §4.1、§4.2、§4.3 | `data/llm/**`、`llm_*.{h,cpp}`、`known_store` 缓存、`Bubble.qml`、阅读与模型设置 section | F |
| C 成本 | `feature/phase2-cost` | §4.4 | `stats_store.*`、`tray.*`、预算设置 section 与 duty class | F |
| D 存储 | `feature/phase2-storage` | §5.2 | `WordsPopup.qml`、`known_store` / `stats_store` 的删除 API | F、B |
| E 界面 | `feature/phase2-interface` | §6.1、§6.2 | `Main.qml` 定位、`theme/**`、`Tokens.qml`、通用设置 section | F |
| G 安装 | `feature/phase2-installer` | §8.1 | `installer/**` | 无 |

### 2.1 并行波次

- **波次 0 串行**：F。先提交一个干净基线，再拆出取词 / 解释 / 成本 / 存储四条接缝；此后各阶段不再改同一个 `app_controller` 或 `SettingsPopup`。
- **波次 1 并行**：A、B、C、E、G。五份文件所有权互不相交，各 worktree 独立推进。
- **波次 2 串行**：D。词表删除要动 B 定下的缓存形状与历史 API，排在 B 之后。
- **波次 3 集成**：i18n 只跑一次（§9），`qml-lint` 棘轮复位，全测试矩阵与真机出图。

### 2.2 冲突规则

- 同一波次两个阶段不能改同一文件；重叠就拆波次或并入同一阶段。
- 枢纽文件（根 `CMakeLists.txt`、`app_controller.*`、`SettingsPopup.qml`、`known_store.*`、`stats_store.*`、`data/llm/**`、`i18n/*.ts`）只由 F 或单一阶段改。
- `i18n` 不进阶段，统一留到波次 3。
- 验收在合并树上重跑，不看分支自述（§9）。

### 2.3 worktree 操作

```
git worktree add ../demo-foundation -b feature/phase2-foundation
git worktree add ../demo-capture    -b feature/phase2-capture
git worktree add ../demo-explanation -b feature/phase2-explanation
git worktree add ../demo-cost       -b feature/phase2-cost
git worktree add ../demo-interface  -b feature/phase2-interface
git worktree add ../demo-installer  -b feature/phase2-installer
# 波次 2，B 合并后再开
git worktree add ../demo-storage    -b feature/phase2-storage
```

### 2.4 F 的接缝交付

F 不实现功能，只交付可并行基线：

- §5.1 落地：`docs/adr/0012` 已记，补密钥边界用例。
- §7.1：新增 `lens_util`（纯函数、无 Qt），更新根目标的依赖图。
- §7.2：关闭 1.0.0 bug（`LlmClient` 看 `QNetworkReply::error()`；通知标题溢出）。
- 接缝：`SettingsPopup.qml` 按分类拆成独立文件；`AppController` 把取词 / 解释 / 成本 / 存储入口拆成 duty class，各阶段只动自己那份；`known_store` / `stats_store` 保持单一文档所有者，后续阶段以 section 加键。
- 判据：`lens_gtest_unit` 与两个 QTest 目标全绿，工作区干净，可开 worktree。

### 2.5 进度

- **F 基础（已完成）**：`lens_util` 目标与 `datePart` / `clampedAdd` 单测；`AppController` 拆成 `CaptureDuty` / `ExplanationDuty` / `CostDuty` / `StorageDuty` 四条职责；`SettingsPopup` 拆成目录、分类页与子页面，与原型一致；§7.2 的两条 1.0.0 bug（`LlmClient` 看 `QNetworkReply::error()`、通知标题溢出）已修；`docs/adr/0013` 记 duty 边界。
- **判据达标**：`lens_gtest_unit` 62 项、`lens_qtest_surfaces` 73 项、`lens_qtest_components` 全绿，`qmllint` 零警告；相关验收树已具备继续开 worktree 的条件。
- **遗留**：§5.1 的密钥边界目前只有现有用例（密钥只写、保存保留非自有键），尚未新增锁住 “密钥不出现在安装目录” 的用例。
- **A / B / C / D / E / G（已整合）**：已落地 Tesseract OCR 与截图 / 自动扫描设置、多义 senses、`es` / `ja` 输出语言、数据驱动的服务商与模型目录、每日预算暂停、词汇删除、Forest / Custom 主题及安装包 OCR payload；本节以下条目以合并树实现为准。

## 3 取词

### 3.1 OCR（阶段 A）

- **目标**：把阶段一的占位（设置里的 `OCR` 与 `自动扫描` 开关、扫描白名单）转为可用，屏幕取词不再只靠选区。
- **要点**：
  - 三条取词路径：截图（不可选中文本，OCR 取字）与扫描（自动模式，稳定快照）走 OCR；选区维持现状。
  - OCR 引擎用 Tesseract；发布包把运行时、依赖 DLL、`eng.traineddata`、许可证与分发说明放在程序目录的 `ocr/` 下，默认只从该目录解析，不依赖系统 PATH。读者也可以在设置里指向自己的 Tesseract（可执行文件与数据目录两项，持久化进设置文档），改路径即重建引擎并重新探测。
  - 扫描管道按 `PRODUCT.md` 的扫描管道一节：前台进程白名单 → 像素 diff → 文字指纹稳定帧 → 候选 → 批量请求 → 限流错峰。
  - 设置提供 OCR / 自动扫描开关、2–5 字母的最小词长、灵敏 / 标准 / 迟钝三档拖拽阈值与进程名扫描白名单；OCR 探测失败时显示运行时或英文数据状态，开关不可用。
  - 发送仍最小化：OCR 只把命中的候选发出去，绝不发送整屏。
- **验收**：`自动扫描` 打开后，在白名单窗口里稳定显示一段英文能自动弹出气泡；设置里的 OCR / 自动扫描 / 扫描白名单与敏感度选项可用；设置里的 Tesseract 路径改写生效（探测结果随之更新），留空回到随包那份；截图入口已于 2026-10-06 从设置下架，`CaptureDuty` 的整屏通道保留、当前无表面触发；离线用例覆盖文字指纹、稳定帧与 OCR 资源探测。
- **依赖**：3.2 的敏感度配置作用于同一管道。

### 3.2 选区弹词敏感度配置（阶段 A）

- **目标**：让读者自己决定选区取词的触发灵敏度，避免过敏感或漏触发。
- **要点**：
  - 设置项放进 “取词与弹窗”，与全局热键、剪贴板策略并列，复用现有下拉与分段控件形态，不新增控件。
  - `最小词长`（下拉，2–5 字母，默认 3）：连续正文里低于该长度的 token 不作候选；读者手选的单个 token 不受此下限约束（见 “选区类型”）。
  - `拖拽阈值`（分段控件，3 档：灵敏 / 标准 / 迟钝）：映射到 `mouse_selection_hook` 的拖拽像素阈值，灵敏更易触发、迟钝更不易。
  - 两项取值持久化进设置文档，作用于同一条取词管道。
- **验收**：切换三档拖拽阈值与最小词长后，真实桌面拖选与连续正文候选行为随之变化；离线用例覆盖三档阈值映射与最小词长下限边界。

## 4 解释与成本

### 4.1 多义解释（阶段 B）

- **目标**：单词通道从 “只取最常见义项” 扩展到可给出多个义项；设置里的 `多义解释` 开关（`UI.md` §4.4）控制。“多重解释” 是本条功能的旧称，不另设功能或开关。
- **要点**：
  - `data/llm/request.word.json` 与 `response.word.schema.json` 支持多条义项；每个词最多保留并展示 3 条 `senses`，关闭开关时只取第一条。
  - 缓存对多义结果分键或存多条。
  - 提示词是数据不是代码，改之前先加载 `writing-prompt`。
- **展示细节**（同步进 `UI.md` §4.3）：
  - 开关关闭时气泡只画一条义项，与现状一致；打开时按义项逐条列出。
  - 条数设上限（默认 3）：超出只留词频最高的几条，其余不画，也不做 “展开更多”。
  - 每条仍是英 / 中两行，沿用气泡现有的字号与颜色；条与条之间用 1 px `line2` 细分隔线分开，不新增外框、编号或项目符号。
  - 音标只跟第一条（最高频义项），义项之间不重复音标。
  - 卡片宽度不变；条数多时气泡变高而非变宽，贴合与倒计时仍按 `UI.md` §4.3 的规则。
- **验收**：开关开时气泡显示多条义项（封顶 3 条）、关时一条；schema 校验覆盖新旧两种形状；`lens_gtest_unit` 与 `lens_qtest_surfaces` 覆盖；真机出图核对多条排版与分隔线。

### 4.2 主流语言拓展（阶段 B）

- **目标**：解释语言从 English / 中文扩展到多种主流语言；界面语言按需扩充。
- **要点**：
  - 解释语言的取值进数据（`outputLanguage`），当前目录提供 `en`、`zh`、`es`、`ja`；新增语言不写死在代码。
  - 界面语言取自同一张语言表：目录里有一种语言就有一套 `i18n/*.ts`，`lrelease` 零 unfinished，`translationFileFor()` 与 `qt_add_translations` 的 `TS_FILES` 各登记一条。
  - `GLOSSARY.md` / `PRODUCT.md` 的解释语言条目随之扩写。
  - **验收**：在 `en` / `zh` 之外，`es` / `ja` 均能端到端往返并校验非空输出；界面语言覆盖语言表的四种，各有一套 `.ts`（零 unfinished），托盘切换无漏串。

### 4.3 模型选择（阶段 B）

- **目标**：从固定 `deepseek-flash` 扩展到可选服务商与手填模型，落地 `UI.md` §4.4 的 “模型服务” 分类。
- **要点**：
  - 设置从 `data/llm/catalog.json` 选择服务商（国内 / 国际 / 其他）：目录项只带 label、分组、base URL 与请求覆盖项，选中只改服务地址，模型是手填输入框、不预填不跟随（`docs/adr/0015`）。
  - 目录列出的是常见地址，页面上写明可能失效、以其官方文档为准。
  - 价目表 `data/llm/pricing.json` 扩到所支持模型；不在表的模型金额记 0 并在设置页标注。
  - 目录校验默认服务商与 HTTPS base URL；运行时按当前地址 / 模型组装请求。
- **验收**：换服务商后请求走新端点、已填的模型不变；换模型后花费按新价目计；缺价目不影响解释。

### 4.4 每日预算上限（阶段 C）

- **目标**：当日的花费到达上限后**暂停取词**，避免无界花钱；阶段一的占位 “预算耗尽托盘态” 随之落地。
- **要点**：
  - 上限可在设置里配置（输入框内左侧标出计价货币符号，取自价目表的显示货币）；设为 0 表示暂停预算上限。到顶后自动扫描、截图与请求暂停，托盘图标进入 “预算耗尽” 态（`UI.md` §4.1、`docs/QML.md` §7）。
  - 计数按当日花费（价目见 `data/llm/pricing.json`），按本地日期，与 `history` / `daily` 一致；跨日读取新日期桶，预算状态自动恢复。
  - 到顶是暂停不是失败：不弹错误通知；读者可自行调高上限或等次日。
- **验收**：设一个很小的上限跑到顶后不再发请求、托盘进入预算耗尽态；离线用例覆盖 “到顶判定” 与跨日重置。

## 5 存储

### 5.1 数据保存在用户数据目录（阶段 F）

- **目标**：用户数据（设置、known-set、缓存、历史）统一保存在 `%APPDATA%\Lens`，不放入程序安装目录。
- **边界**：API key 与其他读者数据继续共用 `%APPDATA%\Lens\settings.json`；安装目录只承载程序和随包分发的静态数据。用户安装方式不改变这个边界。
- **要点**：保留 `main.cpp` 的 `settingsPath()` 与现有首次运行迁移；卸载不删除用户数据，重装后继续读取原文档。仓库根的 `settings.local.json` 仍只是开发来源，不是运行时数据目录。
- **验收**：全新安装后设置、known-set、缓存、历史均落在 `%APPDATA%\Lens`；卸载 / 重装后数据仍可用；API key 不出现在安装目录，密钥边界有用例锁住。

### 5.2 词表删除功能（阶段 D）

- **目标**：读者可以移除词汇列表里的词（`TODO.md` 的 Pending Ideas “词表增加移除功能”）。
- **要点**：词汇弹窗（`UI.md` §4.8）每行加删除入口；确认后从 known-set、缓存与历史中移除该词，并同步扣除对应历史日期的弹词 / 已会 / 新词计数；这是破坏性动作，当前通过确认对话框保护，不提供撤销。
- **验收**：删除后该词不再判为已知、缓存不再命中；`lens_qtest_surfaces` 与离线用例覆盖。

## 6 界面

### 6.1 竖屏 / 横屏自适应（阶段 E）

- **目标**：表面在多屏、竖屏 / 横屏切换时仍落在屏内、方向正确。
- **要点**：锚点按所在屏重算；面板在窄屏下收窄或换行（见 `docs/QML.md` 定位一节与 `UI.md` 表面结构）；屏幕方向变化时重定位。
- **验收**：竖屏与横屏、多屏切换后各表面不越界；真机出图核对。

### 6.2 主题拓展与自定义（阶段 E）

- **目标**：白天 / 黑夜之外增加内置主题，并支持读者自定义配色。
- **要点**：
  - 阶段一已把色值抽进 `theme/`；当前内置 `Light.qml`、`Dark.qml`、`Forest.qml` 三套主题，`Tokens` 另有 Custom 分支允许用户配色覆盖令牌。
  - 自定义配色的入口、持久化与对比度校验（正文 / 次要 / 弱文字均过 WCAG AA）；配色进设置文档。
  - `UI.md` §3 与 `GLOSSARY.md`“主题” 条目改写为 “多主题 + 自定义”。
- **验收**：内置主题至少三套；自定义配色保存后重启仍生效；对比度检查通过。

## 7 工程

### 7.1 重复函数收敛到 `src/util`（阶段 F）

- **目标**：把散落的重复函数抽到 `src/util` 复用（`TODO.md` 的 Pending Ideas “重复函数收敛为 util”）。
- **要点**：先盘点重复（时间 / 统计算术、字符串处理、路径拼装等）；新增 `lens_util` 目标，纯函数、无 Qt 的部分放这里，可离线单测；`lens_core` / `lens_llm` / `lens_app` 依赖它。
- **验收**：重复消除，行为不变；新增单测覆盖抽出的函数；根 `CMakeLists.txt` 的目标图更新。

### 7.2 1.0.0 bug 修复（阶段 F）

- **目标**：关闭 1.0.0 已知 bug（以仓库根的 `BUG.md` 为准）。
- **要点**：已知至少两条——长句翻译时 HTTP 失败、弹窗文字溢出且无法关闭（`BUG.md` §1）；
  `LlmClient` 不看 `QNetworkReply::error()`，200 后传输失败被报成 schema 错误（1.0.0 遗留，见 `TODO.md`）。
- **验收**：每个 bug 有回归用例，修复后由用例锁住；弹窗溢出类在真机出图核对。

## 8 安装

### 8.1 自定义安装页面（阶段 G）

- **目标**：Inno Setup 安装向导的自定义页面符合软件主题（配色、字体、图标）。
- **要点**：用 Inno 的 Pascal 脚本绘制 / 换肤；品牌色与字体与 `UI.md` 一致；扩展 `installer/lens.iss`。构建先由 `windeployqt` 生成 payload，再把 Tesseract `tesseract.exe`、依赖 DLL、`tessdata/eng.traineddata`、许可证和 `THIRD_PARTY_NOTICES.txt` 放入 payload 的 `ocr/`，最后由 Inno 一并安装；缺少完整 OCR 源时构建失败。
- **验收**：安装向导在真机出图，视觉与软件一致；装 / 跑 / 卸一轮通过。

## 9 横切规则

- **i18n 只在波次 3 跑一次**：`lupdate` / `lrelease`，目标零 unfinished（`AGENTS.md` Translations 一节）；阶段内不加文案。
- **QML 栅栏**：`scripts/quality/qml-lint.sh` 零警告，棘轮只降不升，新 `.qml` 同时进 `QML_FILES`。
- **文档同步矩阵**：UI 形态变 → `UI.md` + 原型；实现契约变 → 本文；产品决定变 → `PRODUCT.md` 与 `GLOSSARY.md`；重大取舍 → `docs/adr/`；指标 → `docs/metrics/`。
- **提交**：不写 AI 署名，密钥只从 gitignored 的 `settings.local.json` 读，不入日志。
- **验收在合并树上重跑**，不看分支自述。

## 10 已锁定决定

- **多义由开关控制**，默认关；开启才请求多义，响应使用 `senses`，最多保留 3 条。
- **多义术语统一**：“多重解释” 与 “多义解释” 指同一功能，正式名称为 “多义解释”，不增加第二套协议或开关。
- **模型选择进数据**：`catalog.json` 提供服务商、模型、默认模型、base URL 与请求覆盖项，`pricing.json` 提供价目，不在代码里写死。
- **主题是令牌表**：内置 Light / Dark / Forest 三套主题，另有 Custom 分支；用户配色覆盖令牌并须通过正文、次要、弱文字的 WCAG AA 校验。
- **用户数据位置**：设置、known-set、缓存、历史与 API key 继续放在 `%APPDATA%\Lens`，不随程序进入安装目录；卸载不删除，重装复用。
- **OCR 引擎用 Tesseract**：截图与扫描共用程序目录 `ocr/` 下的 Tesseract；安装包包含运行时、依赖 DLL、英文 `traineddata` 与许可证 / 分发说明，见 ADR-0014。
- **取词敏感度进 UI**：`最小词长` 与 `拖拽阈值` 由读者在 “取词与弹窗” 里配置，拖拽阈值固定 3 档。

## 11 冻结与不在范围内

- `# Pending Ideas` 里未列入 §2 的条目（桌面宠物、选中文本对终端没反应等）不在本阶段。
- 误弹反馈闭环若未列入 §2，保持阶段一的处理。
