# 指标报告

本目录按用途分组存放各次采样的指标报告。文件名带采样日期，是那一天的快照，不随源码漂移而改。

## compile-performance

| 文件 | 回答的问题 |
| --- | --- |
| [`full-build-baseline-2026-10-04.md`](compile-performance/full-build-baseline-2026-10-04.md) | 全量编译的时间花在哪，预编译头与并行度各值多少 |

## code-quality

| 文件 | 回答的问题 |
| --- | --- |
| [`test-coverage-baseline-2026-10-03.md`](code-quality/test-coverage-baseline-2026-10-03.md) | 哪些 C++ 行与 QML 求值位置被测试跑过 |
| [`source-code-review-hotspots-2026-10-03.md`](code-quality/source-code-review-hotspots-2026-10-03.md) | 哪些源码文件与函数应优先审阅 |
| [`selection-classification-accuracy-2026-10-04.md`](code-quality/selection-classification-accuracy-2026-10-04.md) | 单层选区类型判定错在哪里，是否需要多层级策略 |

## runtime-performance

| 文件 | 回答的问题 |
| --- | --- |
| [`qml-user-workflow-baseline-2026-10-03.md`](runtime-performance/qml-user-workflow-baseline-2026-10-03.md) | 真实操作流程下 QML 的时间花在哪 |
| [`qml-automated-scenario-baseline-2026-10-03.md`](runtime-performance/qml-automated-scenario-baseline-2026-10-03.md) | 无鼠标自动化场景下的可重复基线 |
| [`qml-language-list-lazy-loading-validation-2026-10-03.md`](runtime-performance/qml-language-list-lazy-loading-validation-2026-10-03.md) | 语言列表延迟加载是否把成本移出启动 |
| [`qml-tray-menu-lazy-loading-experiment-2026-10-03.md`](runtime-performance/qml-tray-menu-lazy-loading-experiment-2026-10-03.md) | 托盘菜单正文延迟加载前后对比 |

补测清单见 [`coverage/coverage-plan-2026-10-03.md`](coverage/coverage-plan-2026-10-03.md)。
