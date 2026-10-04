# 安装包体积

## 结果

第一次把 Lens 打成安装包（`windeployqt` 铺满 `build-ninja-release/installer/payload`，再交给
`installer/lens.iss` 编译）。改动一次测一次，每次都重新编译同一个安装包：

| 步骤 | 安装包 | 载荷 | 文件数 |
| --- | ---: | ---: | ---: |
| 打出来就能装的基线 | 56,008,073 | 146,071,647 | 1429 |
| ① `--no-compiler-runtime` | 32,203,893 | 120,435,879 | 1428 |
| ② `--no-opengl-sw` | 27,211,805 | 99,795,991 | 1427 |
| ③ `--no-translations` | 26,377,536 | 93,320,635 | 1397 |
| ④ `--skip-plugin-types qmltooling` | 26,157,932 | 92,311,523 | 1386 |

安装包从 56.0 MB 降到 26.2 MB，减掉 53%。载荷里 `data/` 另算 914,944 B，随包分发、不进载荷目录。

## 先修的那一处

`windeployqt` 不给 `--qmldir` 时，它按可执行文件的导入表部署，看不出 QML 里写了什么。这个应用的 QML
是编进二进制的（`qt_add_qml_module`），于是它一个 QML 模块都没铺：装出来的包能启动、托盘图标也在，窗口
一个都建不出来，日志里是 `module "QtQuick.Window" is not installed`，然后进程以 1 退出。`qml/` 目录是空
的，1429 个文件里没有一个属于 Qt Quick。

所以第一版（123,533,140 B 载荷，51,751,612 B 安装包）是**不能用的**：它比现在小，是因为它缺东西。基线
是加上 `--qmldir src/app/qml` 之后的那一版。

## 减掉的四项

1. **`vc_redist.x64.exe`，25.6 MB**，四项里最大的一件，而它本身是一个安装器：跑起来才把运行库装上，不装
   就是 25.6 MB 的死重量。这个包不替微软分发它——VC++ 2015-2022 运行库是 Windows 上的常备件，真要干净
   机器上跑，缺的是 `msvcp140.dll` 家族，那属于发布前的依赖检查（X3），不是往包里塞一个安装器。
2. **`opengl32sw.dll`，20.6 MB**，Qt 的软件 OpenGL 光栅化器。Qt 6 在 Windows 上默认走 D3D11，没有显卡
   驱动时由 WARP 顶上，软件 OpenGL 是更老的一层。装上之后实测：进程加载的是 `d3d11.dll` / `dxgi.dll` /
   `d3d12.dll`，**没有加载 `opengl32sw.dll`**——它在这台机器上从来没被打开过，却一直占着包里五分之一。
3. **`translations/`，6.5 MB**，Qt 自己的界面译文。应用只装自己那套 `:/i18n/lens_*.qm`（`main.cpp` 的
   `loadTranslation`），从不加载 Qt 的目录；导出用的文件对话框走 Windows 原生，按钮上的字由系统画。
4. **`qmltooling/`，1.0 MB**，QML 调试器的插件。装了它的发行版没有人在调试。

## 没动的两项

- **`dxcompiler.dll`（14.3 MB）+ `d3dcompiler_47.dll`（4.7 MB）**：RHI 的着色器编译器，D3D11 后端的
  fallback。它们在空闲时不加载（实测），所以上面那条 “没加载就是不必要” 的判据在这里不成立——真正要用是
  在窗口渲染、着色器编译的那一刻，而那一步 W1 验不了另一台机器。省下的 19 MB 是真的，代价是 “窗口全黑、
  什么都不画”，是这份清单里最坏的一种失败，所以留着。
- **QtQuick Controls 的几套风格插件**（`FluentWinUI3` 2.45 MB、`Imagine` 2.30 MB 等）：应用自己不 import
  `QtQuick.Controls`，但 `QtQuick.Dialogs` 的实现 import 它，风格是运行时按平台挑的。删掉哪几套要靠点开
  导出对话框验证，而那个动作脚本驱动不了（见 `TEST.md` §5 的既有说明），同样不赌。

## 验证

这台机器上，最后一个包：

- `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART` 退出码 0，装到 `%LOCALAPPDATA%\Programs\Lens`
  （`PrivilegesRequired=lowest`，不弹 UAC），装完 97,975,158 B；开始菜单有快捷方式，工作目录指向
  `{app}`。
- 跑起来：托盘图标起来、日志走到 `Lens is up`、`Main.qml` 无告警（`--qmldir` 之前那一版正是在这里刷
  `module "QtQuick" is not installed` 并退出的）。
- 密钥迁移：首次运行把仓库根的 `settings.local.json` 整份拷进 `%APPDATA%\Lens\settings.json`，两份
  sha256 相同（`e69a480e…`），密钥继续可用。
- 回落那一半也是跑出来的：`installer/payload/` 里那份 `lens.exe` 旁边没有 `data/`（装包时才由
  `[Files]` 铺到 `{app}\data`），直接跑它，日志报的词表路径是仓库的 `data\wordlist.txt`，也就是
  configure 烘进来的那条；设置文档仍是 `%APPDATA%\Lens\settings.json`。开发树里的读法没有变。
- `unins000.exe /VERYSILENT` 退出码 0：安装目录整个消失（第一版卸载后留下应用自己写的 `logs/`，
  已在 `[UninstallDelete]` 里收掉），开始菜单快捷方式消失，`%APPDATA%\Lens\settings.json` 的 sha256
  卸载前后相同（`5AAC9D59…`）——卸载不碰读者的密钥与词库，这是有意的。

数字都取自本机（`build-ninja-release`，RelWithDebInfo，Qt 6.9.0，Inno Setup 6，`Compression=lzma2/max`
加 `SolidCompression`）。安装包与载荷大小用 `stat -c%s` 与 `du -sb` 读，不经过压缩率估算。

（同一个载荷重打一次，安装包会在几十 KB 内摆动：每次 `windeployqt` 重铺的文件时间戳不同，而 Inno 把它们
一起压了进去。表里每一行都是那一次实测的值，别拿两行之间的零头当趋势。）
