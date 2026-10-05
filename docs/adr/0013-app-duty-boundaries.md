# 0013 将应用控制器按职责拆分

- **状态**：accepted
- **背景**：`AppController` 同时持有取词、解释、成本和共享文档的生命周期，阶段二的并行阶段会分别扩展这四类行为。继续把入口集中在一个文件会让本应独立的阶段互相冲突。
- **决定**：保留 `AppController` 作为 QML 单例与信号门面，新增 `CaptureDuty`、`ExplanationDuty`、`CostDuty` 和 `StorageDuty`。共享文档仍只有 `KnownStore` 一个所有者，其他职责通过 `StorageDuty` 使用同一份文档。
- **后果**：波次一的阶段可以在各自 duty 或 QML 分类文件中工作；跨职责行为必须通过现有信号或共享存储边界连接。控制器仍是组合根的一部分，不把 duty 注册成 QML 类型。
