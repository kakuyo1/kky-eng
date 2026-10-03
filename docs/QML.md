# QML 事实

> 本文放**这个仓库的 QML 事实**：模块结构、表面与组件的分界、阴影怎么投、定位怎么算、跨线程能不能碰、
> 字体族在哪设。跨文件的判断题见 `CODING_STANDARDS.md`，表面长什么样见 `UI.md`，阶段一的接口契约见
> `PHASE1.md` §4.4。阶段二新增表面一样受本文约束——这里的每一条都是实机上撞出来的，与阶段号无关。

## 1 模块与目录

`src/app/qml/` 根下是九个 `Window`（表面），`src/app/qml/components/` 下是九个可复用件
（`Icon` / `ShadowCard` / `Tokens` / `Segment` / `StatRow` / `MenuRow` / `DropdownField` /
`Switch` / `SwitchRow`）。两组同属一个 QML 模块（`qt_add_qml_module` 的 `QML_FILES` 里写子目录路径
即可），Qt 给模块内每个文件隐式导入本模块的类型，**跨目录照样按类型名解析，谁也不需要写 import**。

判据取 “是不是窗口” 而非 “被几处用到”：`Switch` / `MenuRow` / `DropdownField` 今天各只被一处使用，
它们仍是组件，而按使用次数切会把同类东西拆到两边。九个搬走的文件里没有一处 `qsTr`（文案一律由表面
传入），所以两份 `.ts` 一行未动。

`qt_add_qml_module` 在 `src/app/CMakeLists.txt`，新增 QML 文件要同时改 `QML_FILES` 与
`set_source_files_properties`；`Tokens` 是单例，那一行必须先于 `qt_add_qml_module`。

## 2 窗口

- **各表面是独立 `Window`，不是 `Popup`**（`UI.md` 的 “各表面相互独立，不共用窗口” 正是这么写的）。
- **主窗口不存在，根窗口却必须真的可见**：QML 里嵌在另一个 `Window` 内的 `Window` 会成为它的
  transient child，Windows 在父窗口隐藏时不会把 transient child 显示出来——原先写的 “0×0 且
  `visible: false`” 实测所有表面都不出现，改成 1×1、`opacity: 0`、带 `WindowTransparentForInput`
  的可见窗口后正常（挪到屏幕外也管用，但渲染循环会为 “矩形不与任何屏幕相交” 每次启动记一条警告）。
  这一条靠跑起来验证，不从文档推。
- **窗口的屏幕坐标是 `x` / `y`**：`Window` 没有 `screenX` / `screenY`（那是 `Item` 的属性），
  在函数里写未加限定的 `screenY = ...` 会去写全局属性并报错。
- **显示一张表面是两步**：`visible = true` 不够，还要 `raise()`。实机反馈是托盘菜单被任务栏压住一角
  ——`placePanel` 把卡片的右下角对齐图标中心，卡片下缘本就在 48 DIP 任务栏里进了一半，而点托盘图标
  会把任务栏**激活**，它同样是置顶窗口且会把自己重新提到置顶带顶端，于是刚显示出来的面板落在它下面。
  三处显示路径（`Main.qml` 的 `placePanel`、`Bubble.show`、`SelectionBar.openAt`）都补上 `raise()`，
  没有共用的辅助函数：三个文件各自只有这一行，为它造一个上下文属性不划算。既知边界：这改的是
  **谁在上面**，不是**摆哪儿**——`placeBeside` 仍按整屏夹取，卡片照旧与任务栏重叠，只是现在盖在它上面。
- **点击外部关闭靠钩子**：表面各是独立 `Window`，落在别的窗口上的按下根本不会送进本进程，只有低层
  钩子看得见（`MouseSelectionHook::pointerPressed` → `AppController` 转发 → `main.qml` 的
  `dismissOutside()`）。判 “外面” 用的是卡片矩形而非窗口矩形，四周 26 px 阴影边距算外面。
- **界面语言要调 `QQmlApplicationEngine::retranslate()`**：装翻译器不会让 QML 的 `qsTr` 绑定重算，
  托盘菜单当场变是因为 C++ 那侧自己接了 `uiLanguageChanged`，QML 表面没有对应动作。因此 `engine`
  必须声明在接这个信号的 lambda 之前。
- **换语言时信号的顺序不是随意的**：`setUiLanguage` 先发 `uiLanguageChanged`（它才装翻译器），再发
  `settingsChanged` / `statsChanged`。反过来发，重算 `words()` 时用的还是旧翻译器，词汇弹窗会出现
  表头已变、行没变的样子——实测就是这样，先发 `statsChanged` 那一版没修好。
- **`font.pixelSize` 是整数**：`UI.md` 字号表里的 12.5 / 11.5 / 10.5 px 落不了地，按四舍五入取
  13 / 12 / 11，保住 “最小 11 px” 那条约束。

## 3 阴影

- **`MultiEffect` 不能包着文字用**：它把源渲进一张离屏贴图，本机 125% 下这张贴图被重采样——实测同一个
  字形笔画从 5 像素的锐利边变成 11 像素的糊团，同一个窗口里直接画的那份是清晰的。阴影改由一份只有形状、
  `visible: false` 的副本去投（`ShadowCard.qml` / `SelectionBar.qml` / `Bubble.qml`），文字走直接绘制。
  新增表面照此办理，不要为了阴影把内容塞进特效层。
- **同理，特效 item 只能用源的尺寸**：`MultiEffect` 把自己那份源**铺满整个 item**，所以写成
  `anchors.fill: parent`（parent 是整个窗口，比卡片大）时，卡片的形状会被拉伸到整窗——实测每个弹窗
  底部都多出一层背景，就是这个。要么像现在这样只给位置、让特效按源自己定尺寸（`x: card.x; y: card.y`，
  不写 width / height），要么让 item 与源同尺寸。

## 4 定位

- **贴合按卡片算，不按窗口**：每个表面都是卡片加四边各 26 px 透明阴影边距的窗口，早先拿窗口去贴锚点，
  卡片实际偏出一个边距（气泡纵向还多偏一个 gap），看着就是没贴住选区。气泡与动作条的 `openAt` 现在把
  `shadowMargin` 减掉再算。定位与边缘检测合成一处（`Main.qml` 的 `placeBeside()`：把卡片的右下角放在
  图标**中心**，按锚点所在那屏把窗口整块夹进屏内，上方放不下就翻到下方）；四张面板的 `openNear` 与
  `TrayMenu.openAt` 因此全部删掉，菜单与面板走同一条路。
- **气泡与动作条同侧**：两张卡片都由 “选区完成” 这一个手势触发，故都往选区的**上方**排，上方空间不足
  时才下移。气泡原规格是挂在锚点**下方**，动作条在**上方**，同一个手势引出的两张卡片一上一下，读起来
  是两件不相干的东西各自乱落。`Bubble.qml` 的 `show()` 与 `SelectionBar.qml` 的 `openAt()` 用同一条
  公式。既知边界：那条翻转判据是 `above >= 0`，即拿屏幕绝对坐标 0 当上边界，副屏（负 y）上的选区永远
  判为 “上方没地方” 而一律翻到下方——动作条与气泡现在同病。
- **锚点是物理像素，窗口坐标不是**：钩子拿到的 `pt` 从不虚拟化，而 QML 窗口落在设备无关像素上，两者差
  一个 `devicePixelRatio`（本机 125% 实测：物理 x=275 的松手位置，窗口坐标是 220）；按 1:1 直接用，
  动作条会偏出选区四分之一屏，渲染循环还会记 “矩形不与屏幕相交”。Qt 会把 `QGuiApplication` 设成
  per-monitor aware（实测进程感知级别 = 2），这一条只决定渲染清晰度，不改变上面的换算——交给动作条与
  气泡之前必须先除。
- **物理 → 设备无关那次换算要造新对象**：`Main.qml` 的 `toDip()` 原形如 `payload.x = payload.x / ratio`，
  而 `payload` 是 `controller.bubble` 这类 `QVariantMap` 属性交出来的 JS 对象——**赋值被接受、读回来
  还是旧值**。实测（临时 `console.log`，已删）：`toDip` 里 `ratio=1.25`、`in=936,248`，同一对象走到
  `Bubble.show()` 里仍是 `936,248`。所以自这条注记写下起，每个表面都落在 1.25 倍的目标位置上：气泡跑到
  选区右侧约 234 物理像素，副屏上的动作条干脆掉到屏幕底边。改法是**造一个新对象**再返回，`payload` 的
  其他字段原样拷过去。同一处坏掉的还有 `dismissOutside(toDip(at))`——“点外面关闭” 一直拿物理坐标去比
  设备无关的卡片矩形。
- **气泡的高度要在布局之后再量一次**：`Bubble.show()` 原来在同一次调用里算完位置，而 `height` 是内容的，
  刚赋过 `text` 的 `Text` 还没布局——实测这一步读到 85，卡片实际占 132。卡片于是按 “矮” 定位，随后向下
  长过它本应让开的选区。现在先摆一次（这一帧要画的东西），再 `Qt.callLater(place, payload)` 按真实高度
  摆第二次。同一机制对 `SelectionBar` 无影响：它三项固定、高度不随内容变。
- **`QSystemTrayIcon::geometry()` 给的是设备无关像素，不是物理像素**，而且任务栏自动隐藏时不报值。实测：
  把任务栏勾出来时返回 `(1313, 816, 32, 48)`——1313 DIP 对应物理 1641，正是通知区左缘，48 DIP 是那
  60 物理像素的任务栏厚度。任务栏一收回去它就变 0，而弹窗都是从托盘菜单点开的，点的那一刻任务栏已经
  收回，于是每个面板都落到硬编的右下角、再被 `- width + 60` 推出屏外。`Tray` 现在记住最后一次非空的
  值：窗口不动，过期的答案也是答案。
- **拖动用指针自己的屏幕位置**：`controller.cursorPos()`，也就是 `QCursor::pos()`。另外两条路都实测过、
  都不行。系统移动循环（`startSystemMove()`）在整个拖动过程里没动过窗口，松手才落位，那是跳不是拖；
  handler 的 `activeTranslation` 更糟，它量的是**窗口内**的偏移，移动窗口就改变了决定这次移动的那个值
  ——按手速拖几十个事件，面板从 x=1116 被甩到 x=-3688。两个坑记在这里：`QCursor::pos()` **返回的已经
  是 DIP**，照着钩子那套再除一次 1.25 会让拖动只走 1/1.25 的距离（实测少走 27%）；节拍用 16 ms 定时器
  而不是 `activeTranslationChanged`，因为窗口一旦跟上指针偏移就不再变化、信号随之停止，剩下那段位移
  永远不会被应用。松手那一拍再补一次定位。
- **主屏的任务栏是自动隐藏的**，`GetSystemMetrics(0/1)` 只给主屏尺寸；要整块虚拟桌面得用 76 / 77 / 78 / 79
  四个索引。

## 5 跨线程

**鼠标钩子跑在自己的线程上**。`WH_MOUSE_LL` 的回调在**安装它的那个线程**上执行，Windows 让鼠标等这个
线程应答，等满 `LowLevelHooksTimeout`（默认 300 ms）就放弃——那个线程只要有一段时间不抽消息，鼠标就
整段卡住。原先钩子装在主线程上，而主线程要做的启动工作（建托盘图标、编译 QML、建九个窗口、第一帧的
图形初始化）里没有一处抽消息。现在钩子装在自己的线程上（`hookThreadMain`：先 `PeekMessage` 让本线程
有消息队列，再 `SetWindowsHookEx`，然后 `GetMessage` 泵到底），主线程再怎么卡都与鼠标无关。

回调的发送路径随之为 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 而非 `QTimer::singleShot`：
回调在钩子线程上，信号必须投递到拥有表面的线程，跨线程排队正是 Qt 为这件事提供的；`MouseSelectionHook`
本身仍活在主线程，所以队列落在主线程上。析构走 `PostThreadMessage(WM_QUIT)` 加 `join`，
`UnhookWindowsHookEx` 由钩子线程在退出路上调用——只有它能知道没有回调在途。

**动这个文件时记住**：`MouseSelectionHook` 本身仍活在主线程。过程与实测数字见 §6 与 `PHASE1.md` §4.4。

## 6 其它真机修正

- **`lens` 目标必须是 Windows 子系统**：早先它编译成 console，托盘应用带一个常驻黑窗口，且
  `WIN32_EXECUTABLE` 关着时 Qt 也不会链 `Qt6::EntryPoint`（`Qt6::Core` 的接口按这个属性用生成器表达式
  取舍）。一行 `set_target_properties(lens PROPERTIES WIN32_EXECUTABLE ON)` 同时解决两件事，不必手写
  manifest、也不必手动声明 DPI 感知。
- **字体族设一次，设在 `main.cpp`，不设在 QML**：QML 的 `font.family` 只收一个名字（原先写的是三段逗号
  串，Qt 当成一个名字找，找不到就整站落到 Tahoma——`Text.fontInfo` 在真窗口上读回来的就是这个），而列表
  属性 `font.families` 在 QML 的 font 值类型上根本不存在（赋值即
  `Cannot assign to non-existent property "families"`）。所以 `QGuiApplication::setFont` 收一个
  `QFont::setFamilies({"Segoe UI Variable", "Microsoft YaHei UI Light"})`，QML 侧**不再写字体族**，靠
  继承。中文必须点名落到 Light：Microsoft YaHei UI 的常规体比 Segoe UI Variable 重一档，同权重下中文
  标签看着像加了粗，按墨量实测才定的案。代价是等宽那几个 `Text` 一旦写 `font.family: Tokens.monoFamily`
  就丢掉这条回落，字符串里的中文（128 词的那个单位）走系统回落、比周围略重，只有三处。
- **标题行的图标贴右锚定，不用固定占位**：原先的 `Item { width: parent.width - 40 }` 是按英文标题估的，
  中文标题一变宽就把关闭按钮整个挤出卡片外（放大实测）。改成左锚标题、右锚图标行。
- **界面出现前仍有约 1.5 s，其中 `loadFromModule` 占 810–923 ms**：临时埋点（已删）测出这一段几乎全在
  `engine.loadFromModule("Lens", "Main")` 一个调用里——`QQmlApplicationEngine` 构造 20 ms、两个 context
  property 0 ms、装翻译器 0 ms。数据侧另算：`wordlist` 229 ms + `irregulars` 24 ms。钩子搬走之后这段
  时间**不再冻结鼠标**，所以它只是 “起得慢”，不是卡顿；里面在花什么时间尚未查清（QML 已由
  `qmlcachegen` 预编译，九个窗口的创建与 SVG 图标的栅格化都还没被单独量过）。

## 7 托盘

托盘图标（四状态）用 **C++ `QSystemTrayIcon`**：那是 shell 的东西，没有 QML 对应物。**菜单不是原生的**
——`Menu` 曾按 “Windows 原生可靠” 选过 `QMenu`（`Qt.labs.platform` 的实验性 QML 类型在 6.9 上右键菜单
不生效，仍弃用），代价是画不成 `UI.md` §4.2 的卡片；现改为一个普通表面（`qml/TrayMenu.qml`），`Tray`
只在图标被点时发一个 `menuRequested`。图标只有三个可达状态（自动扫描开 / 解释中 / 已关），第四态（预算
耗尽）要等每日预算上限落地。

