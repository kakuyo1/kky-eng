# ui-prototypes

每张表面一个独立 HTML，是 `UI.md` 的设计对照稿。不是交付物，不进构建，也不代表真实行为：行为契约在 `UI.md`。改了这里的形态，同步改 `UI.md`。

## 文件

| 文件 | 表面 |
| --- | --- |
| `tray-menu.html` | 托盘菜单，含语言子菜单 |
| `stats.html` | 统计弹窗 |
| `words.html` | 词汇弹窗 |
| `year-in-words.html` | 单词年度记录弹窗 |
| `cost.html` | 花费弹窗 |
| `settings.html` | 设置浮层 |
| `bubble.html` | 解释气泡 |
| `selection-bar.html` | 选区动作条 |

## 怎么看

双击打开，或访问 `file:///.../settings.html`。URL 追加 `?theme=dark` 直接看黑夜主题，设置面板里的主题分段也能切。

追加 `?lang=en`、`?lang=ja`、`?lang=es` 按该语言渲染界面，缺省中文。进 README 的四个表面接了它，其余表面和设置里的子页面仍是中文。

## 修改提示

- 一个文件一块表面，自带一份配色令牌（`:root` 与 `html[data-theme=dark]`）。改配色先改 `UI.md` §3.1 / §3.1b，再同步这几个文件，不要只改一个。
- 页面里的交互只是示意：JS 模拟选中、展开与主题切换，用来对视，不是实现。别把原生 `<select>`、`<details>`、密码框的外形当成 QML 的样子去抠像素；要对齐的是层级、间距与状态。
- 静态页面拍不出毛玻璃与阴影的真实观感，QML 侧用 `MultiEffect`，见 `docs/QML.md`。
- `settings.html` 用分类目录：首页只列分类，进入后是分类设置，再进入是词书、API 等子页面。新增设置放进对应分类，不要加长首页，也不要新增一级分类。
- 面板固定 380 × 480，超出的内容在 `.set-body` 内滚动。内容变多先考虑合并成分组或下沉为子页面。
- 扩展分类里的桌面宠物是后续功能的占位，不在阶段一范围。
- 加新表面时，新增一个文件、在这里登记，并在 `UI.md` 的表面表加一行。
- 界面文字四语言各一份，在每个文件脚本开头的 `L` 表里，取值与 `i18n/lens_*.ts` 一致：改文案先改那边，再抄回来。README 的十六张截图由 `scripts/qa/readme-shots.sh` 从这些表生成，写进 `docs/readme-images/<语言>/`，改完记得重跑。
- 文件内的中文同样按项目排版规则写，`zhlint` 可查（`.githooks/pre-commit` 的预算里已登记本文件）。
