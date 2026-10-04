# 0007 两套主题值表住进 qml/theme/

- **状态**：accepted
- **背景**：`Tokens` 单例里每个色值都是 `dark ? <暗> : <亮>` 的三目，两套值逐行交错在同一个文件里；
  再加一个主题、或让读者自己配色，都要从中间把它拆开，而每加一个令牌要同时改两条三目。
- **决定**：`qml/theme/` 下一文件一主题（`Light` / `Dark`），各是一个只读 `QtObject`，属性名与令牌同名；
  `Tokens` 两份都建，按 `dark` 逐令牌转发。类型名 `Tokens` 与全部令牌名一字未动，所以读
  `Tokens.<token>` 的十七个文件、加上 `Main.qml` 那一行 `Binding`，都不必跟着改。
- **后果**：改色只进 `theme/`，`Tokens` 只剩转发。转发写成 `dark ? darkTheme.<name> : lightTheme.<name>`，
  没有走 `palette.<name>`：`palette` 的类型只能是 `QtObject` 或 `var`，前者 qmllint 不认令牌名（会报
  `missing-property`，把警告棘轮顶破），后者写错名字静默变成 `undefined`——两条路都把类型检查换掉了。
  代价是加令牌要在三处各写一行，两份表也都实例化（几十个属性）。半径与 `monoFamily` 不随主题走，
  留在 `Tokens`。
