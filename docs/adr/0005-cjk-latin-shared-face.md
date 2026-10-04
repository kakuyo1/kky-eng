# 0005 中文与拉丁共用一个字体面，混排数字拆 run

- **状态**：accepted
- **背景**：托盘菜单、统计面板等同一行里中英混排时（如 `今日统计` 旁的 `14 词 · ¥0.00`），中文比拉丁看着粗一档，
  同一行里字重忽轻忽重。根因有两层。其一，应用字体栈是 Segoe UI Variable 加中文回落的 Microsoft YaHei UI Light，
  Qt 会按字形覆盖把一行拆成不同字体的 run：拉丁走 Segoe、中文走雅黑。两款字面的笔画密度本就不同，同一个
  `font.weight` 不保证视觉重量一致，中文自然显得更重。其二，等宽数值若把整个混排串都指定成 `Cascadia Code`，
  中文单位会绕开校准过的回落、走 Windows 默认字体；先前的一次改动改为把整段混排值切回应用字体，结果连
  数字和金额也丢了清晰字形，反而更糟。
- **决定**：两处一起改。
  - **统一字面**：`main.cpp` 里应用字体优先用 `Noto Sans SC`，一款同时覆盖拉丁与中文的字面，同字重下两种文字
    出自同一支字体，笔画密度一致；缺失时回退到原系统栈（Segoe UI Variable 加 Microsoft YaHei UI Light），
    不改变没有 Noto 的机器上的行为。
  - **混排拆 run**：新增 `qml/components/MixedText.qml`，把一条混排值按 CJK / 非 CJK 切成若干 run，每个 run 起一个
    `Text`：CJK run 继承应用字体，数字、货币与标点使用 `Tokens.monoFamily`（`Cascadia Code`）。`StatRow`、
    `MenuRow`、词汇弹窗的时间列改用它。字重仍是 400 / 700 两档，`font.weight` 请求值不变。
- **后果**：
  - 混排值多出几个 `Text` 子项，属可接受的渲染成本；换来中文一致性与数字清晰度同时成立。
  - `Noto Sans SC` 是机器字体，随 Windows 语言包而异，缺失时走旧栈——旧栈下中文仍偏重，只是不再比之前更糟。
  - 视觉验证不再靠整桌面截图（会拍到无关窗口，且要真实鼠标）。字体回归用例改用 Qt 屏外场景：`setup.cpp` 用
    `QFontDatabase::addApplicationFont` 显式载入 Segoe、雅黑、Cascadia、Noto 字体文件并设成应用字体，用例用
    `TestCase.grabImage` 把真实 QML 渲染进 PNG 再比对 run 字体，全程不显示窗口、不碰鼠标。字体的准确性由
    `notoFamily` 是否解析成功来保证，而不是由 `font.family` 声明来推断。
  - 规范同步：`UI.md` 与 `DESIGN.md` 的字体条目、`docs/QML.md` 的字体注记都改为优先 Noto Sans SC，混排值由
    `MixedText` 拆 run。
