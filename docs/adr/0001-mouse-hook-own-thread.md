# 0001 鼠标钩子跑在自己的线程上

- **状态**：accepted
- **背景**：`WH_MOUSE_LL` 的回调在**安装它的那个线程**上执行，Windows 让鼠标等这个线程应答，等满
  `LowLevelHooksTimeout`（默认 300 ms）就放弃。钩子原先装在主线程上，而主线程的启动工作（建托盘图标、
  编译 QML、建九个窗口、第一帧的图形初始化）里没有一处抽消息。另起进程循环注入 `mouse_event` 计时实测：
  连续三次 311.9 / 311.6 / 311.7 ms，正压在超时上，这就是读者感到的 “鼠标被抢走”。
- **决定**：钩子装在自己的线程上（`hookThreadMain`：先 `PeekMessage` 让本线程有消息队列，再
  `SetWindowsHookEx`，然后 `GetMessage` 泵到底）。回调的发送路径随之从 `QTimer::singleShot` 改为
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`——回调已不在拥有表面的线程上，跨线程排队正是
  Qt 为这件事提供的。析构走 `PostThreadMessage(WM_QUIT)` 加 `join`，`UnhookWindowsHookEx` 由钩子线程
  在退出路上调用。
- **后果**：`MouseSelectionHook` 本身仍活在主线程，所以回调的队列落在主线程上，动这个文件时要记住这一点。
  先试过的一条更小的改法（把 `install()` 挪到 `app.exec()` 前一行）只降到一次 227 ms——不够，因为第一帧
  的图形初始化就在 `exec` 里。同一探针复测：0 次超 60 ms，最坏 27.7 ms。
