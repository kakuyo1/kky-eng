# Lens

Lens 是一个 Windows 桌面英语学习工具。阅读英文时，拖选一个词、一段名称或一个句子，Lens 会在不离开当前窗口的情况下给出解释。

Lens 常驻系统托盘，没有主窗口。它把交互放在托盘菜单、选区动作条、解释气泡和几个轻量浮层里，尽量不打断阅读。

## 当前能力

- 拖选单个词，查看英文释义、中文释义和音标。
- 拖选命名实体，查看简短的百科式说明。
- 拖选句子，选择翻译或解释长难句、口语和梗。
- 在解释气泡中标记 “已会” 或 “新词”，并在词汇、统计和花费浮层中查看记录。
- 使用 OpenAI 兼容接口，读者自行提供 API 配置。
- 只发送当前选中的文本。应用在发送前会做本地脱敏，设置文件和 API key 保存在读者自己的 `%APPDATA%\Lens` 目录中。

扫描、截图和悬停取词依赖 OCR，当前版本保留设置占位，不会触发这些路径。

## 构建

项目使用 C++ 20、Qt 6、CMake、Ninja 和 MSVC。工具链与机器路径配置见 `AGENTS.md` 和 `config/README.md`。

先确认 `config/paths.json` 中的 Qt 路径有效，然后在 Visual Studio 的 C++ 环境中运行：

```bat
scripts\build\build.bat
```

脚本首次运行时配置 `build-ninja`，之后执行增量构建。需要构建指定目标时，把 CMake 参数传给脚本：

```bat
scripts\build\build.bat --target lens_gtest_unit
```

运行速度更接近交付版本的构建使用单独的 RelWithDebInfo 树：

```bat
scripts\build\build-release.bat
```

## 运行

开发构建完成后运行 `build-ninja\src\app\lens.exe`；需要使用优化构建时运行 `build-ninja-release\src\app\lens.exe`。

首次运行会在 `%APPDATA%\Lens` 创建设置文件。API 配置由读者自行填写，应用不会把密钥写入安装目录、日志或错误消息。

## 测试

测试按用途拆成不需要窗口的 GoogleTest 目标和需要 Qt Quick 场景的 QTest 目标。常用目标如下：

```bat
scripts\build\build.bat --target lens_gtest_unit
scripts\build\build.bat --target lens_gtest_perf
scripts\build\build.bat --target lens_qtest_components
scripts\build\build.bat --target lens_qtest_surfaces
```

构建完成后，可执行文件位于 `build-ninja\test\googletest\` 或 `build-ninja\test\qtest\`。`lens_gtest_integration` 需要真实鼠标和剪贴板，`lens_gtest_smoke` 需要 API key 并会调用真实模型，这两个目标按 `TEST.md` 的人工测试说明运行。

## 安装包

安装包使用 Windeployqt 和 Inno Setup 生成。先构建发布树，再运行安装器目标：

```bat
scripts\build\build-release.bat --target installer
```

产物写入 `build-ninja-release\installer\`，文件名为 `Lens-<version>-setup.exe`。安装包会带上 Qt 运行时和 `data/`，不会带读者的设置文件。当前安装配置以 `installer\lens.iss` 为准。

## 项目结构

- `src/core/`：词根还原、词表过滤、已知词和统计存储。
- `src/llm/`：OpenAI 兼容客户端、协议和响应校验。
- `src/app/`：Windows 选区捕获、托盘和 QML 浮层。
- `data/llm/`：请求提示词与响应 schema。
- `test/`：GoogleTest、QTest 和运行记录约定。
- `docs/`、`PRODUCT.md`、`PHASE1.md`：设计、实现契约和 Qt Quick 说明。

更多开发约定与验证命令见 [`AGENTS.md`](AGENTS.md)，测试的完整说明见 [`TEST.md`](TEST.md)。

脚本目录与后续版本发布命令见 [`scripts/README.md`](scripts/README.md)。
