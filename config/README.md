# `config/` 存什么

机器相关的路径只有这一处：`config/paths.json`。项目**自己**的路径不在这里——它们一律相对仓库根解析，
CMake 侧是 `CMAKE_SOURCE_DIR`（`src/app/CMakeLists.txt`、`test/qtest/CMakeLists.txt` 都在用），脚本侧是
`git rev-parse --show-toplevel`。这两套机制本来就可迁移，再叠一份配置就是第二个真相，迟早漂移。

## 规则

需要一条本机路径时，从这里取，不要在代码里写死。

| 读的人 | 怎么读 |
|---|---|
| CMake | `CMakeLists.txt` 顶部 `string(JSON)`，结果作编译定义传下去，如 `LENS_SYSTEM_FONTS` |
| PowerShell | `scripts/build/paths.ps1` 的 `Get-LensPaths`，构建、质量和 profiling 脚本点源它 |
| 提交钩子 | 经 PowerShell 读；钩子本来就只服务 Windows |
| `CMakePresets.json` | 读不到 JSON，改用 `$env{QT_ROOT}`，由 `scripts/build/build.bat` 从本文件导出 |

`vsRoot` 只有一种机器需要它：VS 被移动过、安装器的数据库里已经没有这个实例（`vswhere` 退出码 0
却什么都不打印）。`build.bat` 先问 `vswhere`，查不到才读这个键。

C++ 不读这个文件：路径在 configure 期由 CMake 变成编译定义，运行期再去解析一份配置只会多一个失败点。

## 换机器

`config/paths.local.json`（gitignore）里出现的键逐键覆盖 `paths.json`。换机器改它，不要动提交进仓库的那份。

## 豁免

下列命中**不算**硬编码，`check_absolute_paths`（`.githooks/pre-commit` 第六项）会跳过：

- `third_party/` 与 `package-lock.json`——vendored 的与生成的，不是我们的代码。
- `*.svg`——每个文件都带 `xmlns="http://www.w3.org/2000/svg"`。
- `*.md`——文档里写路径是叙述，不是解析。
- 含 `http://`、`https://`、`qrc:/` 的行——URL 与 Qt 资源 URI 都不是文件系统路径。
- 驱动器号要落在词的边界上（前面不是字母、数字或下划线）：cmd 的 `%VAR:/=\%` 这类替换语法里那个
  `T:/`，因此不算路径。
- `config/paths.json` 自己——它就是那一处。

取舍与备选方案见 `docs/adr/0006`。
