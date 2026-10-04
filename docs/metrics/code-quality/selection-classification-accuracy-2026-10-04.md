# 选区类型判定准确率报告

## 1、报告信息

| 项目 | 内容 |
| --- | --- |
| 回答的问题 | 当前的单层判定（`classifySelection` 的 kind 与候选序列）在样例集上错在哪里，是否需要一个多层级策略 |
| 生成日期 | 2026-10-04 |
| 被测代码 | `src/core/filter_core.cpp`（单层：出候选 = Word，无候选 = Sentence） |
| 样例集 | `test/eval_corpus.json`，1001 段 |
| 运行方式 | `lens_gtest_unit` 的 `CoreTest.ReplaysTheSampleCorpus`，Debug（`build-ninja`） |
| 运行轮次 | 3 轮 |

判定是纯函数，不依赖时间与环境，三轮结果逐字相同；本报告仍按 `TEST.md` §4 的约定记三轮，并报区间。

## 2、结论

**不需要多层级策略。**单层判定在样例集上的差异全部集中在一条轴上——已知词库与档位词频带被当作候选的删除条件。除此之外 664 段（占 66.3%）全部命中，没有一段是 “本该是句子却被判成单词” 这类层级问题。

| 轮次 | 命中 | 未命中 | 命中率 |
| ---: | ---: | ---: | ---: |
| 1 | 665 | 336 | 66.43% |
| 2 | 665 | 336 | 66.43% |
| 3 | 665 | 336 | 66.43% |

区间：三轮同为 66.43%，极差 0。这个数字是按轴加权的，不是无偏估计——样例集是改后行为的规格，凡是带 known / minFreqRank 的段落按定义就会红。真正的结论在下面的分轴表里。

## 3、分轴结果

| 段落性质 | 段数 | 未命中 | 未命中率 |
| --- | ---: | ---: | ---: |
| 既不带 known 也不带 minFreqRank | 664 | 0 | 0.0% |
| 只带 minFreqRank（档位词频带） | 173 | 172 | 99.4% |
| 只带 known（已知词库） | 163 | 163 | 100.0% |
| 两者都带 | 1 | 1 | 100.0% |

只带 minFreqRank 的那 173 段里有 1 段命中：该段的候选全部落在词频带之外，删不删都一样。

未命中的形态只有两种，都是同一条规则的两个面：

- 候选中位于档位词频带内的词被丢掉。例：`The ubiquitous nature of modern software makes resilience essential.`（minFreqRank 3000）期望 `the / ubiquitous / nature / modern / software / makes / resilience / essential`，实际只回 `ubiquitous / software / resilience / essential`。
- 候选中已被读者标记为已知的词被丢掉。例：`ubiquitous` 单独出现在 known 集合里时期望仍是候选，实际整段被判成 `Sentence`。

## 4、样例集与来源

样例集 1001 段，全部由 `scripts/check-eval-corpus.py` 按 `PHASE1.md` §4.1 与 `src/core/filter_core.h` 的规则校验过（质检 0 项）。按来源分：

| 来源 | 段数 | 期望值怎么来的 |
| --- | ---: | --- |
| 边界串 | 336 | 逐条手写。两字母词、三字母词、全大写缩写、含数字 / 连字 / 点号的粘连串、无元音串、不在词表里的词、词表里恰好收了的行话、混合大小写品牌——每条都能按规则当场核对 |
| 词形还原 | 165 | 逐条手写。后缀规则与不规则表的每一支各若干条，外加规则**故意不做**的还原（nicer / used / seed / news / means） |
| 散文（`prose:` 前缀） | 303 | 手写主张「这段里的每个词表词都是候选」，文本由人读、由检查脚本核对每个 token 都过得了硬过滤 |
| 混合散文 | 186 | 句子里混入缩写、数字、粘连串、行话；期望由规则算出（不是跑实现），notes 里逐字列出每个被丢 token 的原因 |
| 去重 | 10 | 逐条手写，同段同 lemma 只留首次出现 |
| 复述旧条目 | 1 | 旧样例集第 1 条改按新契约写 |

散文的期望是一句手写的主张（“这段的每个 token 都是候选”），混合散文的期望按规则算出；两族都没有跑实现去取答案。边界、还原、去重三族则是逐条手写并逐条可核的，也是这次质检真的抓到东西的那部分（抓出 143 项笔误与规则误解，另有 12 条还原期望经查词表秩后修正）。

## 5、复现

当前实现（判定已改）跑这份样例集，查的是残留误判，应为 0：

```
./scripts/build.bat --target lens_gtest_unit
PATH=/b/qtt/6.9.0/msvc2022_64/bin:$PATH QT_FORCE_STDERR_LOGGING=1 \
  ./build-ninja/test/googletest/lens_gtest_unit.exe --gtest_filter='*Replays*'
python scripts/check-eval-corpus.py
```

每轮输出末尾一行 `corpus: 1001 entries, N mismatched`，N 就是上表里的未命中数。改动样例集或 `filterWords` 之后重跑同一条命令即可。

改前那组数（命中 665 / 未命中 336）：判定改掉之后，同一份样例集在旧实现上再也跑不出来——要复现得把实现退回去，样例集保持当前这份。在 `main` 的 `src/core/filter_core.{h,cpp}`、`src/app/app_controller.cpp`、`test/googletest/unit/filter_core_test.cpp` 上跑：

```
git checkout main -- src/core/filter_core.h src/core/filter_core.cpp \
  src/app/app_controller.cpp test/googletest/unit/filter_core_test.cpp
./scripts/build.bat --target lens_gtest_unit
./build-ninja/test/googletest/lens_gtest_unit.exe --gtest_filter='*Replays*'
git checkout HEAD -- src/ test/
```

旧用例只断言 surface 序列，所以数的是 “未命中的段落数”（按 `Google Test trace` 里的 `corpus #N` 去重）；`Candidate` 的 surface 序列相同即 kind 相同，两条口径在这份样例集上等价。

## 6、本次顺带发现

- `data/irregulars.tsv` 第 3293 行是 `offer → off`，于是 `offer` 的词根被还原成 `off`（`off` 词频序 122，`offer` 897）。`PHASE1.md` §4.1 把 offer 列为 “伪还原，安全落回自身” 的例子，与该数据不符。样例集按数据现状写（`offer → off`），并在该条 note 里标出这处不一致。
- 两字母词干会被 `pushIfInTable` 的三字母下限挡下，因此 being / doing / going / does 都保留原形，不会还原成 be / do / go。

## 7、V3 增量：实体分支与三通道语料

本节追加 V3 的前后测量，保留上面的 T2 原始数字不改。V3 只新增实体边界样例与实体判定，原有
1001 段的 Word / Sentence 期望保持不变。

| 项目 | V3 前 | V3 后 |
| --- | ---: | ---: |
| 语料段数 | 1001 | 1004 |
| Entity 段数 | 0 | 2 |
| 单 token 大写边界 | 未覆盖 | 1（`London` 保持 Word） |
| corpus mismatched | 0（原报告记录） | 0（实测） |
| 独立检查脚本问题数 | 未记录 | 0 |

V3 后的实际输出为 `corpus: 1004 entries, 0 mismatched`，独立脚本输出为
`corpus: 1004 entries, 0 problem(s)`。三轮 `lens_gtest_perf`（Debug，未开启 profile）如下：

| 轮次 | 语料段数 | 中位耗时 | 单段中位耗时 | 最小 / 最大 |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 1004 | 32.9466 ms | 0.0328153 ms | 31.1745 / 53.136 ms |
| 2 | 1004 | 33.0308 ms | 0.0328992 ms | 31.0489 / 75.8548 ms |
| 3 | 1004 | 40.4874 ms | 0.0403261 ms | 31.4795 / 68.4727 ms |

这三轮只说明新增三段后当前实现的测量口径；与第 2 节 T2 的七轮区间不能当作严格性能回归，
因为语料量与运行轮次不同。

## 8 孤立字母 token 走单词通道（2026-10-04）

需求侧观察：先后在 VS Code 里选中 `QML` 与 `qml`，都被判成 `Sentence`。查证后，`Sentence` 是 T2 语料的
刻意设计（全大写与无元音都属硬过滤，`qml` 又不在静态词表）。定夺：**整个选区是单个 letters-only token
（≥2 字母、不分大小写）时改走单词通道**——读者手选一个 token 就是明确在问它；连续正文里的同类 run 仍由
常规过滤处理。响应侧随之放宽：word schema 的 `ipa` 由必填改可选（缩写 / 标识符没有音标），`llm_pure`
不再因空 `ipa` 整批失败，缓存只把缺 `ipa` 键的旧条目当过期丢弃。规则见 `PHASE1.md` §4.1 落地注记。

语料变更：分两轮共 183 条单 token 条目由 `Sentence` 改为 `Word`（`expect` 取小写 token）——第一轮按
“全大写”放开 69 条，第二轮去掉大小写条件再放开 114 条。仍为 `Sentence` 的 99 条单 token 是长度 <2
（`a`）、含数字 / 标点（`MP3`、URL、路径、缩写号）、非 ASCII（重音词、中日韩文）或缺字符的那几类。
`check-eval-corpus.py` 的 `is_lone_token` 规则与实现同步。

| 指标 | 改前 | 改后 |
| --- | ---: | ---: |
| 语料段数 | 1004 | 1004 |
| 单 token 字母（≥2）→ Word | 212 | 395 |
| 单 token Sentence | 213 | 99 |
| corpus mismatched | 0 | 0 |
| 独立检查脚本问题数 | 0 | 0 |

改后实际输出：`corpus: 1004 entries, 0 mismatched`；`python scripts/check-eval-corpus.py` 输出
`corpus: 1004 entries, 0 problem(s)`；`lens_gtest_unit` 45 绿（覆盖
`ClassifiesALoneLettersTokenAsTheWordChannel` 与 `AcceptsAWordResponseWithoutIPAForAnAcronym`）。
旧编号与旧数字不改。

## 9 通道由选区形状定，句子翻译 / 解释生效（2026-10-04）

需求侧观察：选中一句英文，动作条出现但点 翻译 / 解释 只解释了其中一个词，整句翻译 / 解释没有发生。根因：
`classifySelection` 原以 “有候选 = Word” 分流，任何含词表词的英文句子都进 word 通道，只剩 “一个词表词都
没有” 的选区走 sentence。定夺：**通道由选区形状定**——整个选区是单个 letters-only token（≥2 字母、
不分大小写）→ Word；多 token → Sentence；多 token Title Case 短语 → Entity。`filterWords` 对多 token
文本仍照常产出候选（语料里的 `expect` 列表保留，改由它在测试里断言），`check-eval-corpus.py` 的
`expectKind` 与 `expect` 解耦。

句子预设的语义一并定稿（`API.md` §3.2、`PHASE1.md` §5）：解释语言为中文时 `translate` 给字面中文翻译、
`explain` 给中文通俗解释；解释语言为英文时两者都做 “用英文通俗解释这句话”（提示词相同）。气泡按解释
语言只画 `en` 或 `zh` 那一行。

| 指标 | 改前 | 改后 |
| --- | ---: | ---: |
| 多 token → Word | 500 | 0 |
| 多 token → Sentence | 2 | 502 |
| 多 token → Entity | 2 | 2 |
| 单 token → Word | 401 | 401 |
| 单 token → Sentence | 99 | 99 |
| corpus mismatched | 0 | 0 |
| 独立检查脚本问题数 | 0 | 0 |

改后实际输出：`corpus: 1004 entries, 0 mismatched`；`python scripts/check-eval-corpus.py` 输出
`corpus: 1004 entries, 0 problem(s)`；`lens_gtest_unit` 45 绿、`lens_qtest_surfaces` 52 绿。真实模型
往返（句子翻译 / 解释）属人工 / 付费验收，未跑。
