# QML 事实

> 本文放**这个仓库的 QML 事实**：模块结构、表面与组件的分界、阴影怎么投、定位怎么算、跨线程能不能碰、
> 字体族在哪设。跨文件的判断题见 `CODING_STANDARDS.md`，表面长什么样见 `UI.md`。阶段二新增表面一样受本文约束——这里的每一条都是实机上撞出来的，与阶段号无关。

## 1 模块与目录

`src/app/qml/` 根下是十个 `Window`（表面）加一个 `Mask.qml`——它不是窗口而是窗口的宿主，每块屏幕一个
（见 §2 与 `UI.md` 4.11）；`src/app/qml/components/` 下是十二个可复用件
（`Icon` / `ShadowCard` / `Tokens` / `MixedText` / `Segment` / `StatRow` / `MenuRow` /
`DropdownField` / `Switch` / `SwitchRow` / `PathField` / `HotkeyField`），`src/app/qml/theme/` 下是三套主题
各一份色值表（`Light` / `Dark` / `Forest`）加一份动效表（`Motion`）。三组同属一个 QML 模块（`qt_add_qml_module` 的
`QML_FILES` 里写子目录路径即可），Qt 给模块内每个文件隐式导入本模块的类型，**跨目录照样按类型名解析，
谁也不需要写 import**。

值表既不是表面也不是组件：`Tokens` 把 `Light` / `Dark` 两份都建出来，按 `dark` 取一份，逐令牌转发
到同名属性上——表面读的还是 `Tokens.<token>`，换主题仍是一次赋值。色值因此只写进 `theme/`，加第三个
主题是那里多一个文件、`Tokens` 多一臂；为什么不放在 `Tokens` 里见 `docs/adr/0007`。动效表同路进来
（`Tokens.motion.<token>`），见 §9。

判据取 “是不是窗口” 而非 “被几处用到”：`Switch` / `MenuRow` / `DropdownField` 今天各只被一处使用，
它们仍是组件，而按使用次数切会把同类东西拆到两边。十个搬走的文件里没有一处 `qsTr`（文案一律由表面
传入），所以两份 `.ts` 一行未动。

**单个 `.qml` 文件不得超过 32768 字符**（实测：32767 字符能解析，32768 就报 `Invalid argument`）。这条限制
来自 CI 与 `docs/metrics/` 共用的那份 `tree-sitter`（`package.json` 里 pin 的 0.21.1），与文件内容无关；
`scripts/quality/qml-coverage.js` 会在超限时直接点名文件与字符数，而不是抛那句无从下手的 `Invalid argument`。
判据还是上面那条 “是不是窗口”——文件一旦逼近这个数，就是把里面的窗口拆出去的时候（`RemovalQuestion.qml`
就是 2026-10-07 从 `WordsPopup.qml` 拆出来的：那个文件当时 34060 字符，CI 的 QML 覆盖率作业因此整条失败）。

`qt_add_qml_module` 在 `src/app/CMakeLists.txt`：新增 QML 文件改 `QML_FILES` 即可，单例另需
`set_source_files_properties`（本仓库只有 `Tokens` 一个，那一行必须先于 `qt_add_qml_module`，它在那儿
决定要不要往模块的 qmldir 里写单例条目）。`theme/` 的两个文件就是这样加进来的，其余不必动。

## 2 窗口

- **各表面是独立 `Window`，不是 `Popup`**（`UI.md` 的 “各表面相互独立，不共用窗口” 正是这么写的）。
- **一张表面可以不只一个窗口**：`Mask.qml` 的根是 `Item`，里面用 `Instantiator` 每块屏幕建一张 `Window`。
  两条都是实测：`Repeater` 的 delegate 必须是 `Item`，放 `Window` 会报 `Delegate must be of Item type`
  且一个都不建；`Instantiator` 的 `model` 只认数字、列表或 item model，把 `Qt.application.screens` 这个
  QML 列表属性直接交进去也一个都不建、**而且不报错**——所以要传屏幕数（`Qt.application.screens.length`），
  delegate 再按 `index` 回读 `Qt.application.screens[index]`，插拔显示器靠这个读数跟着重建。
- **它是唯一吃焦点的表面**：其余表面一律 `WindowDoesNotAcceptFocus`，这一张要拿指针和 `Esc`，所以照常
  `raise()` 之外还要 `requestActivate()`；`Esc` 仍按仓库惯例走应用级 `Shortcut`（离屏平台上没有窗口是
  激活的，窗口级快捷键测不到）。
- **蒙版必须先离屏再抓屏**：抓的是屏幕 DC（`QScreen::grabWindow`），蒙版自己会在画面里，于是松手与
  取词之间留了 `Mask.qml` 的 `settleMs`。这条只能靠真人看，脚本拍不到。
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

**动这个文件时记住**：`MouseSelectionHook` 本身仍活在主线程。过程与实测数字见 §6。

## 6 其它真机修正

- **`lens` 目标必须是 Windows 子系统**：早先它编译成 console，托盘应用带一个常驻黑窗口，且
  `WIN32_EXECUTABLE` 关着时 Qt 也不会链 `Qt6::EntryPoint`（`Qt6::Core` 的接口按这个属性用生成器表达式
  取舍）。一行 `set_target_properties(lens PROPERTIES WIN32_EXECUTABLE ON)` 同时解决两件事，不必手写
  manifest、也不必手动声明 DPI 感知。
- **字体族设一次，设在 `main.cpp`，不设在 QML**：QML 的 `font.family` 只收一个名字（原先写的是三段逗号
  串，Qt 当成一个名字找，找不到就整站落到 Tahoma——`Text.fontInfo` 在真窗口上读回来的就是这个），而列表
  属性 `font.families` 在 QML 的 font 值类型上根本不存在（赋值即
  `Cannot assign to non-existent property "families"`）。所以 `QGuiApplication::setFont` 收一个
  `QFont::setFamilies({"Noto Sans SC", "Segoe UI Variable"})`，Noto 缺失时回到原系统栈。QML 侧默认靠继承，
  不把单个字体族写到整段混排文本上。纯数字 / 金额可以写 `font.family: Tokens.monoFamily`；混排值使用
  `MixedText` 拆 run，让数字 / 货币保留 Cascadia Code，中文 run 继承应用字体。
- **音标一行同样不能钉字体族**：IPA 扩展块（U+0250-U+02AF）Noto Sans SC 一个字形都没有——本机按码点实测
  ə ɪ ˈ ˌ 等 12 个全缺；Segoe UI Variable 全有，而 Cascadia Code 缺 ˈ ˌ 两个重音符号。`Tokens.monoFamily`
  指的正是 Cascadia Code，所以 `Bubble.qml` 的音标 Text 不写 `font.family`，让上面那条族列表回退接手；钉到
  任何单族都会缺字。
- **标题行的图标贴右锚定，不用固定占位**：原先的 `Item { width: parent.width - 40 }` 是按英文标题估的，
  中文标题一变宽就把关闭按钮整个挤出卡片外（放大实测）。改成左锚标题、右锚图标行。
- **不要在 JS 里遍历 `Controller.words`**：那是 `QVariantList`，逐行读 `row.word` / `row.when` 这类
  QVariantMap 属性在本机实测**每行约 170 ms**——31 行量出 **5.3 秒**，字体测量本身只占几十毫秒
  （`FontMetrics.advanceWidth` 单次 0–8 ms）。这段同步遍历跑在主线程上时，整个 UI 冻结：词汇弹窗
  打不开、系统判无响应、用户结束进程后**既没有 WER 事件也没有 dump**，日志里也什么都不写，看着像崩溃。
  需要全表信息时，优先让**已经在渲染的那几行自己算**（`WordsPopup` 每行把自己的自然宽度报给卡片，
  模型读取只发生在本来就有的 delegate 里），而不是新写一个遍历。
- **量文字宽度用 `FontMetrics`，不是 `TextMetrics`**：`TextMetrics` 靠先写 `text` 再读 `advanceWidth`，
  而这两条都挂在同一个对象上——在绑定里写它自己会读的属性就是 “Binding loop detected”，改成命令式刷新
  才不报警；`FontMetrics.advanceWidth(字符串)` 是方法调用，绑定照常成立。另一处踩坑：`TextMetrics` 也有
  `text` 属性，`testutil.js` 的 `textsUnder()` 按 “有字符串型 `text`” 找 Text，会把探针一起收进去，
  排序在前的探针顶掉真正的行。
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
耗尽）要等每日预算上限落地；琥珀色的那一份 art（`icons/tray-budget-*.svg`）已经在这里，`tray.h` 的类注记
写着它为什么先放进来。

**一族图标只有一个几何**。`icons/lens.svg` 是产品标本身：24×24 的坐标系里，外圆环 `r=7`、描边 2.1，中心
实心点 `r=2.9`。它和 `ui-*.svg` 一样描白色、由 QML 染色，所以托盘菜单的首行用 `Icon` 取它，绿（`ok`）与
灰（`faint`）跟着主题与取词状态走。八个 `tray-<状态>-<任务栏>.svg` 是同一几何各自带色——shell 的位图没有
QML 给它染色，深浅两套只能各写一份。

**可执行文件的图标也是它**：`icons/lens.ico` 由 `icons/lens.rc` 的一句 `ICON` 编进 `lens.exe`，
`src/app/CMakeLists.txt` 里 `enable_language(RC)` 只为它开（根 `CMakeLists.txt` 的 `project()` 没有 RC，
也不去动它）。`.ico` 是二进制，源仍在 `lens.svg`：把白色换成品牌绿（`Tokens.ok` 的浅色值）后按
16 / 20 / 24 / 32 / 48 / 256 栅格化，覆盖 100% 与 125% / 150% 缩放下 shell 的那几个槽位。重做时：

```sh
python - <<'PY'
import cairosvg, io
from PIL import Image
svg = open('icons/lens.svg').read().replace('#ffffff', '#2f9e6e')
big = Image.open(io.BytesIO(cairosvg.svg2png(bytestring=svg.encode(), output_width=256, output_height=256))).convert('RGBA')
big.save('icons/lens.ico', sizes=[(16,16),(20,20),(24,24),(32,32),(48,48),(256,256)])
PY
```

（cairosvg 与 Pillow，两者都不在构建工具链里，只有重做这一张图时才需要。）

## 8 qmllint

下面几条是 qmllint 看不透的地方：照它给的提示改不动，得先知道它盲在哪。

- **`Window.screen` 的成员它当作 `QObject`**：`virtualX` / `width` / `devicePixelRatio` 一律报
  `missing-property`。改用 `Screen` attached 类型它就认得。本机实测（125%，两块屏）：`win.screen.virtualX`
  与 `Screen.virtualX` 都是 0、`width` 都是 1536、`devicePixelRatio` 都是 1.25——取的是同一个屏，所以
  `Main.qml` 的 `trayAnchor()` / `toDip()` 都用 `Screen`。
- **`Qt.application.screens` 它完全看不见**：真实对象上是 QScreen 列表，QtQml 的类型信息里没有这个成员。
  没有等价写法，`Main.qml` 的 `screenFor()` 用 `// qmllint disable missing-property` 把那一个 `for` 豁免掉。
- **delegate 的 `modelData` / `index` 分两层**：delegate 根自己的绑定可以直接写，**嵌套子项**（`Text` /
  `Rectangle` / `TapHandler`）里必须写成 `<delegate 的 id>.modelData`，所以带 required 属性的 delegate 根都得
  有 id。`pragma ComponentBehavior: Bound` 管的是另一件事——跨组件读外层 id（`root` / `list` / `menu` 这类），
  它**不**让嵌套子项看见根上的 required 属性。只加 pragma 只消掉后者：本桶实测 70 → 55，剩下的 15 条全是
  嵌套子项读根上的属性。
- **一个名字在外层文件里 “能用” 不等于被声明**：文件级组件的上下文挂在创建它的上下文上，于是
  `TrayMenu.qml` 里的 `root.showStats()` 会解析到 `Main.qml` 的根（两文件探针实测）。它能跑，但没有任何地方
  声明它，qmllint 对这种名字只给一句 `Unqualified access`、不说该怎么办。表面之间的通信因此走信号：
  `TrayMenu` 现在发 `statsRequested` / `settingsRequested`，由 `Main.qml` 接。

## 9 动效

- **时长与缓动是令牌，住在 `qml/theme/Motion.qml`**：`press`（150 ms）、`pop`（180 ms）、`bubble`
  （220 ms）与一条 `easing`。`Tokens` 以 `readonly property Motion motion` 转发，表面写
  `Tokens.motion.<token>`，QML 里不出现毫秒字面量。它与 `Light` / `Dark` 同处一个目录，因为 `theme/`
  是**值表**所在而不是色值所在。
- **减少动效读的是 Win32，不是媒体查询**：`src/app/system_motion.h` 的 `SystemMotion` 是个
  `QML_SINGLETON`，构造时读一次 `SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)`——
  动画状态在 `pvParam` 指的 BOOL 里，返回值只说读成功没有，读失败按 “没有减少动效” 处理。`Motion.reduced`
  默认取它的值且可写，为真时每个时长解析为 0。**Windows 上没有 `prefers-reduced-motion`**，去找它的
  下一个人应当停在那份文件头。
- **出现动画只有一处，在 `ShadowCard.qml`**：卡片的不透明度跟着它所在窗口的 `visible` 走，配一条
  `Behavior on opacity` 和一条由 `1 - opacity` 推出的 `Translate`（上移 8 px）——一个值驱动两者，减少动效
  时两者一起归零。四张面板、托盘菜单、通知与下拉列表都画在 `ShadowCard` 上，于是 “浮层出现” 写一次就够，
  不必抄六遍。`Bubble` 与 `SelectionBar` 自带卡片、不走这一处，它们的动效只有按压与反馈展开。
  - 例外是托盘菜单的语言列表：它由 `Loader` 在 “该出来” 之后才创建，那时窗口已可见，没有 0→1 的翻转
    可跟。该卡片的 `shown` 因此绑到 `Loader.status === Loader.Ready`，让创建完成本身成为那次翻转。
- **动效拍不到快照，也不该拿快照验收**：能断言的是两条——规格表里每个时长都取自令牌、`reduced` 为真时
  时长解析为 0（`test/qtest/components/tst_motion.qml`），以及卡片确实随窗口在 0 与 1 之间走
  （`tst_shadowcard.qml`）。观感靠一次 QML profiler 运行加人工看。写快照用例时要等 `Tokens.motion.pop`
  走完再抓，否则拍到的是半透明的卡片。
