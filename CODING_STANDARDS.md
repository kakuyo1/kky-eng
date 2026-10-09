# 代码标准

> 只放**审查阶段能判的判断题**：能被机械检查的（格式、排版）一律交给 `.githooks/pre-commit`，这里不重复；
> 所以本文宁缺毋滥，短是正常状态。逐文件的 QML 事实（模块与目录、阴影、定位、线程）见 `docs/QML.md`，
> 注释的写法见 `AGENTS.md` 的 Code Style（不在这里复制一份）。

## C++

- **注释用英文、说 why 不说 what、密度与邻座一致**。写法的正文在 `AGENTS.md`。
- **日志走 `LENS_*` 宏**，不用 `qDebug` / `std::cout`。Qt 自己的 qDebug / qWarning 由
  `src/util/qt_log.h` 折进同一个 logger，源位置指向 Qt 调用点。
- **一个设计若与既有注释冲突，改注释与改代码放进同一个提交**。留在树里的旧注释比没有注释更坏——下一个
  读者会照着它做。

## QML

- **阴影来自一份只有形状的 `visible: false` 副本**，不要把内容包进 `MultiEffect`：它把源渲进离屏贴图，
  125% 下被重采样，同一个字形会从锐利边变成糊团。
- **表面的定位按卡片算，不按窗口算**：窗口四周有 26 px 透明阴影边距，每次都要把 `shadowMargin` 减掉。
- **显示一张表面是两步**：`visible = true` 之后还要 `raise()`，否则可能被任务栏压住。
- **跨线程回调只能排队投递**到拥有表面的线程（`QMetaObject::invokeMethod(..., Qt::QueuedConnection)`）。

四条背后的实测数字与既知边界见 `docs/QML.md` §2 / §3 / §4 / §5。
