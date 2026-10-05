# 0006 机器路径集中到 config/paths.json，项目自己的路径继续相对解析

- **状态**：accepted
- **背景**：同一个 Qt 前缀在这棵树里手写了四遍——`CMakePresets.json` 三处，`qml-snapshot.ps1`、
  `qml-profile.ps1`、`mouse-stall-probe.ps1` 各一处；`.githooks/pre-commit` 另存着 VS 的 LLVM 目录与
  zhlint 的配置路径。换机器或换 Qt 版本时要改几处，全凭记忆。提出过更彻底的一版：建 `config/` 装下
  **所有**绝对路径，并禁止项目里再出现任何硬编码路径。
- **决定**：分两类。
  - **机器相关的值**进 `config/paths.json`（提交，带本机的值，开箱可用）；`config/paths.local.json`
    （gitignore）逐键覆盖，是换机器时唯一要改的文件。读它的有四处：CMake 用 `string(JSON)`，PowerShell
    用 `scripts/build/paths.ps1`，提交钩子经 PowerShell；`CMakePresets.json` 读不了文件，改用 `$env{QT_ROOT}`，
    由 `scripts/build/build.bat` 从该文件导出。
  - **项目自己的路径不进去**，继续相对仓库根解析：CMake 侧 `CMAKE_SOURCE_DIR`（`LENS_DATA_DIR`、
    `LENS_QTEST_DIR` 已是这个模式），脚本侧 `git rev-parse --show-toplevel`。C++ 不读 JSON——路径在
    configure 期由 CMake 变成编译定义（`LENS_SYSTEM_FONTS`），运行期少一个失败点。
  - **禁令限定范围**：`.githooks/pre-commit` 第六项只看本次提交动到的、属于我们自己的文件，并跳过
    `third_party/`、`package-lock.json`、`*.svg`、`*.md`、含 `http(s)://` 或 `qrc:/` 的行，以及
    `config/paths.json` 自身。
- **后果**：
  - 装下所有绝对路径、并禁止硬编码的那一版被否掉，因为它同时制造三件事：与 CMake 两份真相；提交进仓库的绝对路径让
    CI 与第二个 clone 失效；那条禁令按字面执行不了——只 `third_party/nlohmann/json.hpp` 就有 424 处
    命中（URL 与许可证文本），每个 svg 都带 `xmlns`。会立刻变成噪音的门禁会被关掉，比没有更糟。
  - 代价：`CMakePresets.json` 不再自带 Qt 路径，裸跑 `cmake --preset ninja-qt6` 得先有 `QT_ROOT`。顶层
    `CMakeLists.txt` 因此在找不到 Qt 时给出指名道姓的 `FATAL_ERROR`，而不是 Qt 自己那句
    `package configuration file not found`。`scripts/build/build.bat` 是文档里的入口，它自己会导出。
  - `config/paths.json` 是唯一允许写绝对路径的文件，这条豁免在第六项检查里是显式的、带注释的。
