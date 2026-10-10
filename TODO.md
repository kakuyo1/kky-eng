# TODO by agents

## 添加档位词书数据

档位（`GLOSSARY.md`“档位”）定义为 “默认使用者已完全掌握该档位主流词书的全部单词”，落地需要 8 份词表：B1–B2 / C1–C2 / CET-4 / CET-6 / TEM-4 / TEM-8 / 雅思 / TOEFL。

现状：`data/wordlist.txt` 是纯词频表，不含考试标签，一个档位都拼不出来；当前以词频阈值近似（`PHASE1.md` 4.1 `minFreqRank`），会误判。CET-6 词书含大量非高频词，词频前列也混着 CET-4 之外的专业词。

待办：确定词书来源（自备 txt / 公开词表），落地后把 known-set 预置从 “词频阈值” 改为 “词书集合”。

## 更新源镜像

现状：阶段三的检查更新只用 GitHub Releases，地址写在 `data/update.json`，国内网络需经代理才能访问。

待办：实测国内可达的镜像（版本接口与发布页两处），定下后只改 `data/update.json`，并在 `SECURITY.md` 写明出站地址。

## 不规则表把 offer 归给了 off

现状（2026-10-04）：`data/irregulars.tsv` 中有 `offer` 到 `off` 的映射，不规则表命中即裁定，选中 `offer` 时请求、缓存、气泡标题与已会标记都按 `off` 处理。`PHASE1.md` §4.1 却把 `offer` 列为安全落回自身的例子，文档与数据矛盾。

待办：确认映射是否来自 WordNet 异常表。`scripts/data/gen_irregulars.py` 的补漏只有 women 与 people；是数据错就修生成脚本或加例外，是文档错就改对应说明。

## 定位气泡 hover 抖动

现状：真机日志出现过悬停状态在 79 ms 内翻两次，导致倒计时重启和反馈按钮反复收放。受控复现失败；卡片只向下展开，上沿不动，现有几何证据不支持光标因展开落到卡片外的解释。

待办：真机再遇到时，核对 `HoverHandler` 记录的光标与卡片屏幕矩形，确认是光标出界还是窗口可见性变化。

## HTTP 200 下的传输失败分类

`LlmClient` 仅凭 HTTP 状态码选择错误路径，没有核对 `QNetworkReply::error()`。返回 200 后中途断开可能被报告为 schema 错误。补充离线失败用例，确认网络错误的提示措辞，再修正分类。

## QML profiler 完整场景入口

`scripts/profiling/qml-profile.ps1` 的 `Scenario` 是字符串参数；`-Scenario` 单独使用会失败，默认 `full` 又未设置 `LENS_QML_PROFILE_SCENARIO`，因此不会启动自动退出的完整场景。统一脚本与复现文档的完整场景约定。

# Pending Ideas

## 桌面宠物

## 选中文本对终端没反应

## 竖屏自适应

## 重复函数收敛为 util

## 自定义安装页面

## 弹窗边缘检测优化

## 检查更新功能

## level 加上自定义词表支持

## 卸载时可选是否删除所有用户信息

