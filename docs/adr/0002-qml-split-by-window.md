# 0002 QML 按 “是不是窗口” 分两处

- **状态**：accepted
- **背景**：`src/app/qml/` 下十八个文件平铺在一起，表面与可复用件混着，看不出哪些是入口。
- **决定**：九个 `Window`（表面）留在 `qml/` 根，其余九件可复用件（`Icon` / `ShadowCard` / `Tokens` /
  `Segment` / `StatRow` / `MenuRow` / `DropdownField` / `Switch` / `SwitchRow`）进 `qml/components/`。
  判据取 “是不是窗口” 而非 “被几处用到”。
- **后果**：`Switch` / `MenuRow` / `DropdownField` 今天各只被一处使用，它们仍是组件——按使用次数切会把
  同类东西拆到两边，多一处引用就得挪一次文件。两组同属一个 QML 模块（`QML_FILES` 里写子目录路径即可），
  Qt 给模块内每个文件隐式导入本模块的类型，**跨目录不需要写 import**。九个搬走的文件里没有一处 `qsTr`
  （文案一律由表面传入），所以两份 `.ts` 一行未动，只有 CMake 的路径要跟着改。
