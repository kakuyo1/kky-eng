# TODO

## 中文字面比拉丁重一档

现状：同一字重下，中文看着比旁边的拉丁粗一档，用户报中文字体加粗混乱。

已排除：字体族解析。`Text.fontInfo` 在真窗口上读回 `Segoe UI Variable` 400/700，权重是真两档（同串同字号墨量差 49%），不是合成粗、也不是没生效。逗号串写法也已改掉（原 `Tokens.fontFamily` 是三段逗号串，Qt 当单个名字解析、整站落到 Tahoma）。

剩下的机制：Microsoft YaHei UI 的常规体本身比 Segoe UI Variable 的常规体重，所以 400 的中文标签看着像加了粗——中文界面里标签全是中文、旁边夹着数字与 `API` 这类拉丁，就成了忽粗忽不粗。

已尝试：`main.cpp` 里用 `QFont::setFamilies({"Segoe UI Variable", "Microsoft YaHei UI Light"})` 设应用字体（QML 的 `font.families` 属性根本不存在，只有这一层带得动列表），中文回落 Light。按墨量，中文正文对英文正文的比值降到 1.4 倍（中文笔画本就密，1.4 属正常区间），但用户仍报未修好。

待办：真机上把 400 与 700、中文与拉丁两两并排渲染、逐行量墨量，先定位他看到的究竟是哪一处（可能根本不是正文，而是某个胶囊或数值）。若确属字面色差，候选是换掉整个中文面（随包带一份 Noto Sans SC 之类）或整体改用雅黑——两者都要动 `UI.md` §3.2 并记 ADR。

## 添加档位词书数据

档位（`CONTEXT.md`「档位」）定义为「默认使用者已完全掌握该档位主流词书的全部单词」，落地需要 8 份词表：B1–B2 / C1–C2 / CET-4 / CET-6 / TEM-4 / TEM-8 / 雅思 / TOEFL。

现状：`data/wordlist.txt` 是纯词频表，不含考试标签，一个档位都拼不出来；当前以词频阈值近似（`PHASE1.md` 4.1 `minFreqRank`），会误判——CET-6 词书含大量非高频词，词频前列也混着 CET-4 之外的专业词。

待办：确定词书来源（自备 txt / 公开词表），落地后把 known-set 预置从「词频阈值」改为「词书集合」。

## 定位气泡 hover 抖动

现状：真机日志里出现过悬停状态在 79 ms 内翻两次（`bubble hover false` / `true` / `false`），倒计时会因此重启、反馈按钮反复收放。受控复现失败——把光标停在卡片上再微动，`hover true` 一直没翻；而交接单给的机制（展开撑高卡片、光标落到卡片外）在几何上不成立，卡片只向下长，上沿不动。`Bubble.qml` 只加了 140 ms 的下降沿护栏，进入仍即时，动画本身没动。

待办：真机上再遇到时，在 `HoverHandler` 里把光标与卡片矩形的屏幕坐标一起记下来，先看清是光标真的出界，还是别的东西在改 `visible`。

## 接 CI

现状：全库没有 CI。`lens_gtest_unit` 从建起就标着零网络、零密钥、可进 CI，却一直没有地方跑；`PHASE1.md` §9 的结论与 `TEST.md` 的运行记录全靠人工转写。提交前只有 `.githooks/pre-commit` 那一道文档门，C++ 侧没有任何自动门。

待办：等切片三（AppController + QML 表面）落完再动手——那时 `lens_gtest_unit` 覆盖的东西才算稳定，现在接等于给一个还在动的目标上锁。届时至少需要：Windows runner；Qt DLL 上 `PATH`（`unit` 链 `lens_llm`，`integration` 链 `lens_app`）；`lens_gtest_unit` 作必过项；`lens_gtest_integration` 里那两例真机用例不进 CI（要真实鼠标与剪贴板）；`.githooks/pre-commit` 的文档检查在 CI 里跑一份等价实现。
