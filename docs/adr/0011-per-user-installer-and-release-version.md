# 0011 按用户安装，发布绑定源码版本与安装包

- **状态**：accepted
- **背景**：Lens 的密钥、词汇记录与自启属于当前读者；部署 QML 应用还需要 Qt 的运行时模块，单独复制可执行文件不能交付完整界面。
- **决定**：以 `windeployqt --qmldir` 部署运行时，再由 Inno Setup 打包，安装到当前用户目录。设置保留在 `%APPDATA%\Lens`，不随包分发，卸载不删除。自启由安装后的应用按自身路径设置。
- **发布**：CMake 的 `project()` 维护版本，生成安装器版本；对应提交绑定注释 tag 与 GitHub Release。发布脚本重新构建安装包，只使用该版本的 CHANGELOG 段落；入口见 `scripts/README.md`。
- **后果**：安装器依赖 Windows 工具链与 Inno Setup。设置不受安装路径与升级影响；打包必须包含 QML 模块，不能以损坏的更小载荷作为体积优化结果。
