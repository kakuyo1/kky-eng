# LLM 协议（阶段一：单词通道）

> 本文件描述 Lens 与释义模型之间的线上格式。契约来源见 `PHASE1.md` §4.3 与 §5；**实际生效的定义在 `data/llm/` 目录**，代码只组装与校验，不内联任何提示词或字段名。

服务：DeepSeek（OpenAI 兼容，BYOK）。端点 `POST {baseUrl}/chat/completions`，请求头 `Content-Type: application/json` 与 `Authorization: Bearer <API-KEY>`。

## 1 通道

请求属于哪个通道在发送前就已经定好，通道决定用哪套提示词与哪套响应 schema。文件按通道一一对应：

| 通道 | 请求 | 响应 schema | 状态 |
|---|---|---|---|
| 单词 | `data/llm/request.word.json` | `data/llm/response.word.schema.json` | 阶段一落地 |
| 实体 | `data/llm/request.entity.json` | `data/llm/response.entity.schema.json` | 阶段二（`PHASE1.md` §2 占位） |
| 句子 | `data/llm/request.sentence.json` | `data/llm/response.sentence.schema.json` | 阶段二（同上） |

## 2 请求体

字段与默认值全部来自该通道的 `request.<通道>.json`：

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

- `messages[0]` 是系统提示词，取自 `request.word.json` 的 `systemPrompt.template`；末句由 `{outputLanguage}` 占位符按解释语言替换（`outputLanguage.en` / `outputLanguage.zh`）。提示词本身是英文。
- `messages[1]` 是待查内容，**一行一个**。单词通道只发单词本身，发送前做脱敏兜底（邮箱 / 长数字 / URL 掩码成 `<email>` / `<num>` / `<url>`）。
- `thinking` 必须显式设成 `disabled`：DeepSeek 默认开启思考模式，开着会白付 reasoning token 且更慢。
- `max_tokens` 必须显式设：JSON 被截断时接口不报错，只把 `finish_reason` 置为 `length`。
- `response_format` 为 `json_object`，因此系统提示词里必须出现 `json` 字样并给出格式示例，缺了模型会一路吐空白到 `max_tokens` 用尽。

## 3 响应体

外层是 OpenAI 风格的 envelope，模型真正产出的 JSON 是 `choices[0].message.content` 里的**字符串**：

```json
{
  "choices": [
    {
      "finish_reason": "stop",
      "message": {
        "content": "{\"results\":[{\"word\":\"ubiquitous\",\"en\":\"present everywhere\",\"zh\":\"无处不在的\",\"example\":\"Phones are ubiquitous.\"}]}"
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
      "en": "present everywhere",
      "zh": "无处不在的",
      "example": "Phones are ubiquitous."
    }
  ]
}
```

字段含义以 `data/llm/response.word.schema.json` 为准，那里同时是**校验的唯一真源**——代码从 schema 的 `properties.results.items.required` 读出必填字段名，所以往 schema 里加一个字段，校验立刻跟着变，不会与本文档漂移。

| 字段 | 类型 | 说明 |
|---|---|---|
| `word` | string | 原样回显输入的词，用于逐词核对 |
| `en` | string | 一行英文释义 |
| `zh` | string | 一行中文释义 |
| `example` | string | 一句例句；阶段一不进浮层，但 schema 保留 |

## 4 校验规则

响应是**第三方不可信数据**，任一项不过即整批失败，不静默丢词（`PHASE1.md` §4.3）：

1. 外层是 JSON 对象，且 `choices` 非空；
2. `finish_reason` 必须是 `stop`——`length`（截断）、`content_filter`、`insufficient_system_resource`、`aborted` 一律判失败；
3. `message.content` 能解析成 JSON 对象，且带 `results` 数组；
4. `results` 每一项是对象，且**具备 schema 要求的全部字段**；
5. `word` / `en` / `zh` 非空（`example` 允许为空，它不进浮层）；
6. 回显的词与请求**逐一对应**：不多、不少、不重、不拼错。返回顺序不作要求，代码按请求顺序回填。

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

## 6 相关文件

- `data/llm/request.word.json` — 请求模板、系统提示词、输出语言行
- `data/llm/response.word.schema.json` — 响应 schema（校验真源）
- `src/llm/llm_protocol.{h,cpp}` — 加载与访问上述文件
- `src/llm/llm_pure.{h,cpp}` — 组装请求体与校验响应，均无网络
- `test/main.cpp` — `--filter` 里用固定样例锁住上述行为
