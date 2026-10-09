# 脚本目录

脚本按用途分组。命令默认从仓库根目录执行，机器相关路径统一由 `scripts/build/paths.ps1` 读取 `config/paths.json`。

## 目录

| 目录 | 用途 |
| --- | --- |
| `build/` | Debug、RelWithDebInfo 构建和机器路径读取 |
| `data/` | 词形数据生成与样例集独立校验 |
| `profiling/` | QML profiler、QML 结构指标和 trace 解析 |
| `qa/` | QML 快照、桌面驱动、截图和鼠标停顿探针 |
| `quality/` | QML lint、C++ / QML 覆盖率和 AST 指标 |
| `release/` | 安装包构建和 GitHub Release 发布 |

## 构建与验证

```bat
scripts\build\build.bat
scripts\build\build-release.bat
```

```sh
sh scripts/quality/qml-lint.sh
sh scripts/qa/readme-shots.sh          # 重画 README 的四语言截图，源是 ui-prototypes/
```

具体测试目标、覆盖率口径和桌面验证方法见 [`TEST.md`](../TEST.md)。

## 发布

版本号只在根 `CMakeLists.txt` 的 `project(Lens VERSION ...)` 中维护。提交版本变更和
`CHANGELOG.md` 后，可单独生成安装包：

```bat
scripts\release\build-installer.bat
```

```powershell
powershell -File scripts/release/publish-release.ps1 -Version 1.1.0 -WhatIf
powershell -File scripts/release/publish-release.ps1 -Version 1.1.0
```

发布脚本要求干净工作树、CMake 版本与 CHANGELOG 对应，然后重新构建安装包并核对 release QML lint。
它推送当前分支，创建并推送指向当前提交的注释 tag `v<version>`，用该版本的 CHANGELOG 段落发布
`Lens-<version>-setup.exe`。`-WhatIf` 只做前置核对，`-Draft` 创建草稿。

先完成 `gh auth login`，按 `TEST.md` 完成测试，并人工验证安装后的应用。若 tag 推送后发布失败，脚本保留 tag；
核对 tag 指向与本地产物后，可直接用 `gh release create --verify-tag` 恢复发布。具体安装和版本取舍见 ADR 0011。
