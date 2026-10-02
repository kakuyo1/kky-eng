# TODO

## 添加档位词书数据

档位（`CONTEXT.md`「档位」）定义为「默认使用者已完全掌握该档位主流词书的全部单词」，落地需要 8 份词表：B1–B2 / C1–C2 / CET-4 / CET-6 / TEM-4 / TEM-8 / 雅思 / TOEFL。

现状：`data/wordlist.txt` 是纯词频表，不含考试标签，一个档位都拼不出来；当前以词频阈值近似（`PHASE1.md` 4.1 `minFreqRank`），会误判——CET-6 词书含大量非高频词，词频前列也混着 CET-4 之外的专业词。

待办：确定词书来源（自备 txt / 公开词表），落地后把 known-set 预置从「词频阈值」改为「词书集合」。

## 接 CI

现状：全库没有 CI。`lens_gtest_unit` 从建起就标着零网络、零密钥、可进 CI，却一直没有地方跑；`PHASE1.md` §9 的结论与 `TEST.md` 的运行记录全靠人工转写。提交前只有 `.githooks/pre-commit` 那一道文档门，C++ 侧没有任何自动门。

待办：等切片三（AppController + QML 表面）落完再动手——那时 `lens_gtest_unit` 覆盖的东西才算稳定，现在接等于给一个还在动的目标上锁。届时至少需要：Windows runner；Qt DLL 上 `PATH`（`unit` 链 `lens_llm`，`integration` 链 `lens_app`）；`lens_gtest_unit` 作必过项；`lens_gtest_integration` 里那两例真机用例不进 CI（要真实鼠标与剪贴板）；`.githooks/pre-commit` 的文档检查在 CI 里跑一份等价实现。
