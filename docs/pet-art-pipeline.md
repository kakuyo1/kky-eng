# 桌宠美术流水线：重绘或换一套画风

本文记录 1.2.0 重绘白色小狗时实际走过的步骤，并把其中的脚本整理成可复用的工具。以后要换画风，照着做即可。
范围只包括**外观**：小狗的形状、眼睛、嘴与配饰的位置。动作的帧数、帧率、锚点和数据结构不在本文范围内，
改它们要改 `data/pet/*.json` 并跑测试（见 `docs/history/PHASE3.md` §3.1、§3.8）。

## 1。先定的约束

这些约束由渲染器与应用的加载检查决定，不满足就会在启动时被拒绝或画错位置。

- **画布 88 × 88**，每帧一格。`data/pet/sprites/` 里所有 sheet 的宽度是 “帧数 × 88”，高度 88。
  表情 sheet 是一排 88 × 88 的格子。`sync_sprites.py` 会先核对这些尺寸。
- **狗盒 18 × 17 格**。渲染器里狗盒左上角在画布（24，24），网格从狗盒的（6，6）开始，每格 3 单位，
  单位就是画布像素。所以格子 `(x, y)` 的左上角在画布上是 `(24 + 6 + 3x, 24 + 6 + 3y)`。
- **配饰坐标有两种单位**：`pixels` 用狗盒单位（同上，0 是狗盒左上角，可为负，表示头顶的空间）；
  `rects` 用格子。把 `rects` 平移时，位移必须是 3 的倍数。
- **眼睛**：两格，相距 3 格；嘴在眼下。表情 sheet 由 `EYE_PATTERNS` 在每个眼睛格内画出瞳孔形状。
- **原创**：形状只能来自本仓库里的源文件或新画的配方，不能从任何外部图片采样。
  旧版素材（从 Undertale 参考图采样）不得回到仓库，它留在 `assets-private/`，已被 `.gitignore` 忽略。

## 2。文件地图

| 文件 | 作用 | 怎么改 |
| --- | --- | --- |
| `ui-prototypes/pet-prototype/assets/pet/sprite.json` | 网格源：`head`、`body`、`eyes`、`mouth`，以及配饰的 `pixels` / `rects` | 用 `apply_grid.py` 写入；配饰用 `refit_accessories.py` |
| `ui-prototypes/pet-prototype/assets/pet/manifest.json` | 每个动作的 sheet 路径、帧数、表情索引，以及配饰的资源与 `supportedActions` | 一般不改；动作的帧数改了要同时改 `animations.json` |
| `scripts/pet/recipes/*.json` | 形状配方，`grid_from_shapes.py` 据此生成网格 | 复制 `original-dog.json` 后改数字 |
| `ui-prototypes/pet-prototype/tools/build-assets.py` | 渲染器：读上面两个文件，画出全部 PNG | 只在换了**结构**（比如去掉某个姿势）时才改 |
| `data/pet/sprites/` | 应用真正读取的副本 | 只由 `sync_sprites.py --apply` 复制，不手改 |
| `ui-prototypes/pet.html` | 原型设置页，内嵌了全部 PNG 的 base64 | `cd ui-prototypes/pet-prototype && npm run embed` 重新生成 |

渲染器里还有几处**写死的几何**，换形状后要逐帧看一遍：

- `sleep()` 里的鼻子与微笑坐标 `face_pixels`，是按老狗的脸画的。
- `BOOK` 的位置 `(-13, 14, 4, 36)` 是书本的狗盒单位框，不随狗的形状移动。
- 各动作的姿势函数（`standing`、`sleep`、`pokes`、`thought` 等）都直接用 `sprite.json` 的网格，所以换网格后姿势会跟着变，
  但它们没有为新形状做单独调整。

## 3。流程

脚本命令都在仓库根目录运行。需要 Pillow（`python -I` 下要能 import PIL）。

**第 0 步：备份旧版。**把现有的 `sprite.json` 与 `data/pet/sprites/` 复制到 `assets-private/<名字>/`，
它会被忽略，不进仓库。

**第 1 步：得到新网格。**二选一：

- 用配方：复制 `scripts/pet/recipes/original-dog.json`，改其中的形状（`ellipse` 用中心 `cx, cy` 与半径 `rx, ry`，
  `rect` 用 `cols` 与 `rows`，都是闭区间），然后预览：
  `python -I scripts/pet/grid_from_shapes.py recipe.json`。满意后输出到临时文件：
  `python -I scripts/pet/grid_from_shapes.py recipe.json --out "$TEMP/grid.json"`。
- 手绘：直接编辑 `sprite.json` 的 `head` 与 `body`（每行 18 个字符，17 行）。

**第 2 步：写入源。**先预览差异，确认只改了四段，再写入：

```sh
python -I scripts/pet/apply_grid.py "$TEMP/grid.json"           # 只显示差异
python -I scripts/pet/apply_grid.py "$TEMP/grid.json" --write
```

**第 3 步：眼睛与嘴。**眼睛两格、相距 3 格，中点尽量接近身体中线（见第 4 步的输出）。
嘴在眼下，两端对齐眼睛。直接改 `eyes` 与 `mouth`，或在第 4 步用 `--eyes` 设置。

**第 4 步：配饰对位。**先看质心，再选位移：

```sh
python -I scripts/pet/refit_accessories.py          # 只报告
python -I scripts/pet/refit_accessories.py --dx-head 12 --dx-face 15 --dx-body 8 --write
```

报告会打印头、身体的质心与眼睛中点（单位）。`--dx-head` 移动帽子、毛线帽、嫩芽、王冠；
`--dx-face` 移动两副眼镜；`--dx-body` 移动领结。1.2.0 的取值是 12、15、8，依据见第 4 节。

**第 5 步：渲染并核对。**

```sh
python -I scripts/pet/sync_sprites.py
```

它先运行 `build-assets.py`，再核对每个 sheet 的尺寸，最后列出每个 PNG 与 `data/pet/sprites` 是否相同（不改任何文件）。
尺寸不对会直接报错，不会进入下一步。

**第 6 步：看图。**用复合预览逐个动作、逐帧检查，不要只看一张：

```sh
python -I scripts/pet/compose_check.py "$TEMP/check.png" --action study --frame 2
```

然后用 Read 工具打开图片。预览的第一格是裸狗加表情，其余每格加一件配饰。重点看：眼睛是否落在框内、
配饰是否贴着头、领结是否在脖子下面、睡觉与书本两帧。

**第 7 步：复制到应用。**

```sh
python -I scripts/pet/sync_sprites.py --apply
```

**第 8 步：原型页与原型测试。**

```sh
cd ui-prototypes/pet-prototype
npm run embed
node --test test/*.test.js
```

**第 9 步：应用测试。**尺寸与层序由 `lens_gtest_unit` 的 `pet_data_test` 检查，桌宠的加载与配饰由 `lens_qtest_pet` 检查，
设置页由 `lens_qtest_surfaces` 检查。`scripts\build\build.bat` 会自动跑 qml-lint。

**第 10 步：对比原型与设置页。**运行 `scripts/qa/pet/pet-capture.mjs`，看它输出的动作对比图。
只能确认两边画得一致，不能确认好看，好看要人来判断。

**第 11 步：文档。**如果形状或配饰的描述变了，同步 `UI.md`、`PRODUCT.md` 与 `GLOSSARY.md`。

## 4。1.2.0 的经验

- **质心不在画布中心。**1.2.0 的头质心在第 31.0 单位，而画布中心在第 32.5 单位，相差半格。
  这是因为耳朵的形状不完全对称。配饰按画布中心对齐（即 +12 单位），而不是按质心对齐，
  因为戴在头上的东西要看整张图的中线。换形状后，先看报告再决定位移。
- **位移回放。**用 1.2.0 的旧备份作为起点，依次执行第 2 步与第 4 步（`--dx-head 12 --dx-face 15 --dx-body 8 --eyes "7,3 10,3"`），
  得到的 `sprite.json` 与仓库里的 1.2.0 版本的 JSON 内容相同（文本排版可能不同）。这说明流程是可复现的。
- **不要用通用的 JSON 序列化重写 `sprite.json`。**它的格式是人手写的：网格每行一行，坐标数组单行，配饰每件一行。
  通用序列化会把每个数字展开成一行，diff 无法阅读。`apply_grid.py` 与 `refit_accessories.py` 都只替换需要改的段落。
- **整数单位。**`rects` 的位移不是 3 的倍数时脚本会报错。原因是格子只能整格移动。
- **原型 `pet.html` 是生成物。**改了 `assets/pet` 却不跑 `npm run embed`，设置页里仍然是旧图，而且旧图会留在 git 中。

## 5。换角色，不是换画风

本文的流程只换**同一个角色**的外观。当前代码只有一个角色：`main.cpp` 固定读取 `data/pet`，
`PetAssets::load` 固定读取其中的 `pet.json`，设置页的类型选择也只有 “白色小狗” 一项。
`docs/history/PHASE3.md` §3.8 写的是 “按角色目录解析”，但代码没有实现这一点。

要增加第二个角色，需要：

1. 把 `data/pet` 改成按角色 id 选择的目录（例如 `data/pet/<id>/`），并让 `main.cpp` 从设置里读取角色。
2. 让设置页的类型选择列出所有角色，并在切换时清空配饰、回到待机（规格 §3.5）。
3. 新角色的动作、锚点与配饰都要重新定义，可以复制 `data/pet/*.json` 作为起点，并跑 `lens_gtest_unit` 的配置测试。

## 6。脚本索引

| 脚本 | 作用 | 会改文件吗 |
| --- | --- | --- |
| `scripts/pet/grid_from_shapes.py` | 配方 → 网格；打印 ASCII 预览 | 只在给 `--out` 时写临时文件 |
| `scripts/pet/apply_grid.py` | 网格 → `sprite.json` 的四段 | 只在给 `--write` 时写 |
| `scripts/pet/refit_accessories.py` | 报告质心，按位移对位配饰，可设眼睛 | 只在给 `--write` 时写 |
| `scripts/pet/sync_sprites.py` | 渲染、核对尺寸、比对；`--apply` 复制到 `data/pet/sprites` | 只在给 `--apply` 时写 |
| `scripts/pet/compose_check.py` | 一帧加表情与每件配饰的审查图 | 只写指定的 PNG |
| `scripts/pet/recipes/original-dog.json` | 1.2.0 小狗的配方，可复现当前网格 | 复制后改 |
