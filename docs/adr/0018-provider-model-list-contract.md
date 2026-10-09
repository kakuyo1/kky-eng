# 0018 模型列表接口按服务商声明，缓存属于读者

- **状态**：superseded by ADR-0020
- **背景**：解释 API 兼容 OpenAI，不代表模型列表也有统一 endpoint。Google 的原生列表使用不同路径、响应 envelope、ID 前缀与认证头；安装目录在 Windows 上可能只读。
- **决定**：`catalog.json` 为每家服务商声明模型列表方法、路径、认证头、响应映射、能力过滤与官方文档来源。运行时只请求 HTTPS 的目录端点；Google 使用原生 `models` 接口，其他已配置兼容服务使用各自 base URL 下的 `/models`。新列表写入读者 `%APPDATA%\Lens\settings.json` 的 `MODELS`，打包目录只保留离线种子。空、错误、超时、超限或无效响应均不覆盖缓存；每个异步响应携带请求 provider，只更新对应服务商。模型列表在打开服务商下拉时刷新，已有用户缓存与目录种子先行显示。
- **后果**：目录校验与解析器需理解显式 response mapping；认证策略只能使用允许的请求头，不能放进 URL 或日志。未提供用户凭据时无法完成各账号私有模型清单的实网验证，官方 endpoint 元数据仍由离线 fixture 覆盖。
