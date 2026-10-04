# LLM 协议
> 本文件描述 Lens 与释义模型之间的线上格式。契约来源见 `PHASE1.md` §4.3 与 §5；**实际生效的定义在 `data/llm/` 目录**，代码只组装与校验，不内联任何提示词或字段名。

服务：DeepSeek（OpenAI 兼容，BYOK）。端点 `POST {baseUrl}/chat/completions`，请求头 `Content-Type: application/json` 与 `Authorization: Bearer <API-KEY>`。`baseUrl = https://api.deepseek.com`；配置文件原值 `https://api.deepseek.com/anthropic` 是 Anthropic 格式的 base，对本调用不适用，已改。模型取 `settings.local.json` 的 `MODEL`（缺省 `deepseek-flash`）：`deepseek-flash` 即 DeepSeek-V4.1-Flash（支持 JSON Output），同代另有 `deepseek-v4-pro`；旧名 `deepseek-v4-flash` 仍被接受但模型已停服，请求实际由 V4.1-Flash 承接并按其价计费。

## 1 通道

请求属于哪个通道在发送前就已经定好，通道决定用哪套提示词与哪套响应 schema。文件按通道一一对应：

| 通道 | 请求 | 响应 schema | 状态 |
|---|---|---|---|
| 单词 | `data/llm/request.word.json` | `data/llm/response.word.schema.json` | 已落地，四字段含 IPA |
| 实体 | `data/llm/request.entity.json` | `data/llm/response.entity.schema.json` | V3 已落地，两字段（en / zh） |
| 句子 | `data/llm/request.sentence.json` | `data/llm/response.sentence.schema.json` | V3 已落地，两字段 + 翻译 / 解释两个预设 |

`llm_protocol.{h,cpp}` 加载并校验这些文件：缺文件、非法 JSON、必填值为空一律抛，**不回落内置默认值**——回落会正好掩盖这次抽离要防的漂移。

## 2 请求体

组装后的请求体。除 `model` 来自 `settings.local.json` 的 `MODEL`，其余字段与默认值都来自该通道的 `request.<通道>.json`：

```json
{
  "model": "deepseek-flash",
  "messages": [
    { "role": "system", "content": "…systemPrompt.template，其中 {outputLanguage} 已替换…" },
    { "role": "user",   "content": "ubiquitous\nresilience" }
  ],
  "response_format": { "type": "json_object" },
  "thinking":       { "type": "disabled" },
  "max_tokens": 4096,
  "stream": false
}
```

- `messages[0]` 是系统提示词，取自所选通道与预设的 `systemPrompt.template`；末句由 `{outputLanguage}` 占位符按解释语言替换（`outputLanguage.en` / `outputLanguage.zh`）。提示词本身是英文。
- `messages[1]` 是待查内容，**一行一个**。单词只发词根，实体只发实体名，句子只发选中的句子；发送前做脱敏兜底（邮箱 / 长数字 / URL 掩码成 `<email>` / `<num>` / `<url>`）。
- `thinking` 必须显式设成 `disabled`：DeepSeek 默认开启思考模式（effort=high），开着会白付 reasoning token 且更慢。思考模式下 `temperature` 无效；`top_p` 仅思考模式生效（有效区间 0.95–1.0），非思考模式固定 1.0。
- `max_tokens` 必须显式设（非思考模式缺省 8K）：JSON 被截断时接口不报错，只把 `finish_reason` 置为 `length`。
- `response_format` 为 `json_object`，因此系统提示词里必须出现 `json` 字样并给出格式示例，缺了模型会一路吐空白到 `max_tokens` 用尽。

## 3 响应体：单词

外层是 OpenAI 风格的 envelope，模型真正产出的 JSON 是 `choices[0].message.content` 里的**字符串**：

```json
{
  "choices": [
    {
      "finish_reason": "stop",
      "message": {
        "content": "{\"results\":[{\"word\":\"ubiquitous\",\"ipa\":\"/juːˈbɪkwɪtəs/\",\"en\":\"present everywhere\",\"zh\":\"无处不在的\"}]}"
      }
    }
  ],
  "usage": { "prompt_tokens": 0, "completion_tokens": 0, "total_tokens": 0 }
}
```

把 `content` 再解析一次，得到本项目的载荷：

```json
{
  "results": [
    {
      "word": "ubiquitous",
      "ipa": "/juːˈbɪkwɪtəs/",
      "en": "present everywhere",
      "zh": "无处不在的"
    }
  ]
}
```

字段含义以 `data/llm/response.word.schema.json` 为准，那里同时是**校验的唯一真源**——代码从 schema 的 `properties.results.items.required` 读出必填字段名，所以往 schema 里加一个字段，校验立刻跟着变，不会与本文档漂移。

| 字段 | 类型 | 说明 |
|---|---|---|
| `word` | string | 原样回显输入的词，用于逐词核对 |
| `ipa` | string | 音标，两侧带斜杠；可选，缩写（如 `qml`）没有音标时可以缺席或为空 |
| `en` | string | 一行英文释义 |
| `zh` | string | 一行中文释义 |

## 3.1 响应体：实体

实体响应同样嵌在 `choices[0].message.content`，只带 `en` / `zh` 两行，不带单词专属的 IPA，也不回显实体名：

```json
{
  "results": [
    {
      "en": "A major city in the United States.",
      "zh": "美国的一座大城市。"
    }
  ]
}
```

`en` 与 `zh` 各是一行百科式说明。**实体响应不回显实体名**：App 已知道选中的文本，让模型逐字复现长串或带标点的名字只会漂移；解析后由 App 把选中文本盖到 `title` 上。实体通道只有一个 `default` 预设，动作条上的 “翻译” 和 “解释” 共用它。

## 3.2 响应体：句子

句子响应也只有 `en` / `zh` 两个字段（同样不回显标题），`request.sentence.json` 提供 `translate` 与 `explain` 两个预设：

```json
{
  "results": [
    {
      "en": "The room seemed changed.",
      "zh": "这个安静的房间感觉不一样。"
    }
  ]
}
```

`en` 与 `zh` 是译文或讲解，App 把选中的句子盖到 `title` 上（同实体）。两个预设共用 schema，任务随**解释语言**改变：解释语言为中文时，`translate` 给字面中文翻译、`explain` 给中文通俗解释；解释语言为英文时，两者都只做 “用英文通俗解释这句话”，提示词因此相同。句子响应不包含 `ipa`，也不进入单词缓存与 verdict；气泡按设置只画所选解释语言那一行（`en` 或 `zh`），另一行虽在响应里但不显示。

`usage` 在 envelope 层，不在载荷里，因此**不参与上面那套校验**：`parseUsage()` 从同一个响应体独立读 `prompt_tokens` / `completion_tokens`，缺失按 0 计并记警告。统计少一笔可忍，把一次成功的解释整批丢掉不可忍；`usage` 与解释内容是否合法互不影响（`PHASE1.md` §4.3）。金额不由线上格式给出，见 §6。

## 4 校验规则

响应是**第三方不可信数据**，任一项不过即整批失败，不静默丢词（`PHASE1.md` §4.3）：

1. 外层是 JSON 对象，且 `choices` 非空；
2. `finish_reason` 必须是 `stop`——`length`（截断）、`content_filter`、`insufficient_system_resource`、`aborted` 一律判失败；
3. `message.content` 能解析成 JSON 对象，且带 `results` 数组（容忍 Markdown ``` 围栏或前后闲话：提取首个 `{` 到末个 `}`）；
4. `results` 每一项是对象，且**具备所选 schema 要求的全部字段**；
5. `en` / `zh` 非空；单词通道额外要求 `word` 非空，`ipa` 可空（孤立缩写的单词没有音标）；
6. 单词通道回显的词与请求**逐一对应**：不多、不少、不重、不拼错；实体 / 句子不回显，按请求顺序取结果（`title` 由 App 盖上），模型把一段拆成多条时按顺序**合并**（各条 `en` / `zh` 以换行拼接）。返回顺序不作要求，代码按请求顺序回填。

失败原因会经 Qt 翻译（`tr()` / `QCoreApplication::translate()`）后交给 `LlmClient::failed(QString)`，译文见 `i18n/lens_zh_CN.ts`。

## 5 错误码

| 状态码 | 含义 |
|---|---|
| 400 | 请求格式错误 |
| 401 | API key 无效或缺失 |
| 402 | 账户余额不足 |
| 422 | 请求参数无效 |
| 429 | 请求过于频繁，被限流 |
| 500 | 服务端错误 |
| 503 | 服务过载 |

`failed(message)` 按上表分类；**任何分支都不得回显密钥**。

## 6 金额与价目

金额不由线上格式给出，本地按 `data/llm/pricing.json` 算（`llm_pricing.{h,cpp}`）。**价目是数据**：按模型名给输入 / 输出单价（每百万 token）与币种，改价是改数据，不重编译；价目缺失或模型不在表内时金额记 0 并记警告，不影响解释。

**牌价与展示币种分开**（2026-10-03）：表里 0.15 / 0.6 是厂商的**美元**牌价，界面显示人民币。做法是 `display` 块写明目标币种与一个乘数（`multiplier`）及其取值日期（`asOf`），**牌价原样不动**——把 `currency` 直接改成 `CNY` 等于宣称厂商按人民币报价，那是说错。换算只在 `Pricing::cost()` 里发生一次，`currency()` 返回展示币种，故统计、花费、托盘提示三处无需各自知道还有另一种币。乘数是数据，会过期：启动时把乘数与日期一起记进日志，看到 ¥ 想问 “哪来的” 时不必翻文件。`display` 块缺失时展示币种回落到厂商币种。

## 7 相关文件

- `data/llm/request.word.json`、`request.entity.json`、`request.sentence.json` — 请求模板、预设、系统提示词与输出语言行
- `data/llm/response.word.schema.json`、`response.entity.schema.json`、`response.sentence.schema.json` — 响应 schema（校验真源）
- `data/llm/pricing.json` — 价目数据，算金额用（§6）
- `src/llm/llm_protocol.{h,cpp}` — 加载与访问上述协议文件
- `src/llm/llm_pricing.{h,cpp}` — 加载价目、把 `Usage` 折成金额
- `src/llm/llm_pure.{h,cpp}` — 组装请求体、校验响应、读 `usage`，均无网络
- `test/googletest/unit/llm_pure_test.cpp` — 用固定样例锁住上述行为
