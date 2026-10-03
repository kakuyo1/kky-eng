# TODO

## 把 qmllint 的警告修到 0

现状（2026-10-03）：`scripts/qml-lint.sh` 已接进 pre-commit、CI 与 `scripts/build.bat`，阈值 70、只降不升。
直接拿 `qmllint` 喂松散的源文件会得 525 条（那些文件没有模块上下文，`Tokens` 与同目录组件全都解析不了），
走 `qt_add_qml_module` 生成的响应文件才是真数。

已做（130 → 70）：`controller.*` / `tray.*` 那 53 条。QML 模块改挂 `lens_app`，两个类成了真正的 QML 单例，
QML 侧改名成 `Controller.` / `Tray.`；为什么值得改、代价是什么，见 `docs/adr/0004`。

待办，按桶：

- delegate 里的 `index` / `modelData.*`（约 20 条）：声明成 `required property`。
- 嵌套组件里读外层 id（`root.*` / `list.*` / `clickable`，约 12 条）：`pragma ComponentBehavior: Bound`。
  **这会改 id 的解析语义**，改完托盘菜单与四张面板要人眼过一遍——本机拍不到那几张。
- `[missing-property]` 那 38 条：`Main.qml` 的 `toDip()` / `placeBeside()` 经由 `var` 拿 `QScreen`，
  `virtualX` / `width` / `devicePixelRatio` 因此无法验证；`SelectionBar.qml` 在一处 `HoverHandler` 上读
  `pressed`，该属性不存在，**疑为真 bug**。

每修完一桶，把 `scripts/qml-lint.sh` 的 `BASELINE` 降到新的实测值。

## 选区钩子的合成拖拽用例一直红着

现状（2026-10-03）：`lens_gtest_integration` 里 `SelectionHook.ReportsASynthesisedDragAtItsReleasePoint` 稳定
失败——测试在自己进程的探针窗口上合成 40 px 拖拽，钩子什么都没报。拿改动前的树复跑结果一样，所以不是新近的
回归；`PHASE1.md` §9 记的 “15 例中 14 例通过” 在这条上已经过期。

待办：先确认它是不是被 “钩子认自己的窗口” 那条规则挡下的（`PHASE1.md` §4.4），再核对用例的意图——若探针窗口
注定被拒，用例该换一个别的进程的窗口，或改成断言 “被拒绝”。

## 中文字面比拉丁重一档

现状：同一字重下，中文看着比旁边的拉丁粗一档，用户报中文字体加粗混乱。

已排除：字体族解析。`Text.fontInfo` 在真窗口上读回 `Segoe UI Variable` 400/700，权重是真两档（同串同字号墨量差 49%），不是合成粗、也不是没生效。逗号串写法也已改掉（原 `Tokens.fontFamily` 是三段逗号串，Qt 当单个名字解析、整站落到 Tahoma）。

剩下的机制：Microsoft YaHei UI 的常规体本身比 Segoe UI Variable 的常规体重，所以 400 的中文标签看着像加了粗——中文界面里标签全是中文、旁边夹着数字与 `API` 这类拉丁，就成了忽粗忽不粗。

已尝试：`main.cpp` 里用 `QFont::setFamilies({"Segoe UI Variable", "Microsoft YaHei UI Light"})` 设应用字体（QML 的 `font.families` 属性根本不存在，只有这一层带得动列表），中文回落 Light。按墨量，中文正文对英文正文的比值降到 1.4 倍（中文笔画本就密，1.4 属正常区间），但用户仍报未修好。

待办：真机上把 400 与 700、中文与拉丁两两并排渲染、逐行量墨量，先定位他看到的究竟是哪一处（可能根本不是正文，而是某个胶囊或数值）。若确属字面色差，候选是换掉整个中文面（随包带一份 Noto Sans SC 之类）或整体改用雅黑——两者都要动 `UI.md` §3.2 并记 `docs/adr/`。

## 添加档位词书数据

档位（`CONTEXT.md`「档位」）定义为「默认使用者已完全掌握该档位主流词书的全部单词」，落地需要 8 份词表：B1–B2 / C1–C2 / CET-4 / CET-6 / TEM-4 / TEM-8 / 雅思 / TOEFL。

现状：`data/wordlist.txt` 是纯词频表，不含考试标签，一个档位都拼不出来；当前以词频阈值近似（`PHASE1.md` 4.1 `minFreqRank`），会误判——CET-6 词书含大量非高频词，词频前列也混着 CET-4 之外的专业词。

待办：确定词书来源（自备 txt / 公开词表），落地后把 known-set 预置从「词频阈值」改为「词书集合」。

## 定位气泡 hover 抖动

现状：真机日志里出现过悬停状态在 79 ms 内翻两次（`bubble hover false` / `true` / `false`），倒计时会因此重启、反馈按钮反复收放。受控复现失败——把光标停在卡片上再微动，`hover true` 一直没翻；而交接单给的机制（展开撑高卡片、光标落到卡片外）在几何上不成立，卡片只向下长，上沿不动。`Bubble.qml` 只加了 140 ms 的下降沿护栏，进入仍即时，动画本身没动。

待办：真机上再遇到时，在 `HoverHandler` 里把光标与卡片矩形的屏幕坐标一起记下来，先看清是光标真的出界，还是别的东西在改 `visible`。

## 接 CI

现状（2026-10-03 起步）：`.github/workflows/ci.yml` 已落地——push / PR 到 `main` 触发，Windows runner +
Qt 6.9.0（`jurplel/install-qt-action`，覆盖 preset 里那份本机路径），跑 `lens_gtest_unit` 作必过项，并
一并构建 `lens`，好让 `src/app` 的编译错误也进闸。此前 `lens_gtest_unit` 从建起就标着零网络、零密钥、
可进 CI，却一直没有地方跑。

待办：`lens_gtest_integration` 里那两例真机用例不进 CI（要真实鼠标与剪贴板），`smoke` 要真实密钥，
两者都留人工。文档侧目前只有 `.githooks/pre-commit` 一道门，CI 里还没有等价实现——加之前先问它值不值
这份维护成本（hook 已经守着同一批检查，但只在本地提交时跑）。
