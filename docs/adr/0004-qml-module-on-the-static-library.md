# 0004 QML 模块挂在静态库上

- **状态**：accepted
- **背景**：`AppController` 与 `Tray` 定义在 `lens_app`（静态库）里，而 QML 模块原先挂在可执行文件 `lens`
  上。`qt_add_qml_module` 只注册它自己那个 target 的 C++ 类型，这两个类因此对 QML 引擎是隐形的——QML 一直靠
  `setContextProperty` 拿到它们，而 context property 是运行时概念：qmllint 看不见它，130 条警告里 53 条是读写
  这两个名字；更糟的是属性名写错（`Controller.buble`）不会报错，只会读成 undefined。
- **决定**：`qt_add_qml_module` 改挂 `lens_app`；两个类加 `QML_SINGLETON` + `QML_NAMED_ELEMENT(Controller /
  Tray)` + `create()` 工厂，实例由 `main()` 通过 `provide()` 交进去，所有权在 `create()` 里设回 C++（否则
  引擎会删一个它没建的栈对象）；QML 侧 53 处 `controller.` / `tray.` 改成 `Controller.` / `Tray.`。
- **后果**：静态库的 QML 插件必须显式链进可执行文件——`target_link_libraries(lens PRIVATE lens_appplugin)`。
  Qt 把带着 `Q_IMPORT_PLUGIN` 的 init 对象挂在插件 target 上，所以写一次插件名就够；漏了它的表现很隐蔽：表面
  照常加载、启动日志正常，只是每个 `Controller` / `Tray` 引用都是 ReferenceError。`lens_gtest_integration`
  链接 `lens_app`，因此也被带上 Qt Quick——它本来一个 QML 符号都不需要。换来的是 QML↔C++ 的接口面从此受
  lint 检查（130 → 70 条，剩下的见 `TODO.md`）。
