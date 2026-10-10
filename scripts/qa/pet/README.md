# 桌面宠物原型验证脚本

验证 `ui-prototypes/pet-prototype/`（原型）与 `ui-prototypes/pet.html`（设置页，由原型代码生成）的实际效果。
两个脚本都用无头 Chrome 驱动页面，不弹出真实窗口，不触碰桌面。

## 前置条件

1. 原型依赖已安装：`cd ui-prototypes/pet-prototype && npm ci`（脚本从这里取 `playwright` 与 `vite`）。
2. Chrome 已安装。路径从 `config/paths.json` 的 `chromeExe` 读取，换机器时写进 `config/paths.local.json`；
   环境变量 `CHROME` 可以临时覆盖。
3. 如果改过 `pet-prototype/src/` 或 `embed/settings.template.html`，先重新生成设置页：
   `cd ui-prototypes/pet-prototype && npm run embed`。两个脚本测的是仓库里已有的 `pet.html`，不会自己生成它。

## pet-capture.mjs：原型与设置页逐动作对比

```sh
node scripts/qa/pet/pet-capture.mjs [输出目录]
```

对每个动作（11 个）和每件配饰（7 件），在原型和 `pet.html` 上于触发后 0、150、350 ms 各截一张画布，
再把两边拼成一张对比图。

- 输出目录默认是 `$TEMP/pet-capture`。
- `montage-acts.png`、`montage-accs.png` 是拼图，每行一个动作或配饰，左 3 张是原型，右 3 张是设置页。
- 截图文件名为 `<动作或配饰>-<proto|page>-<帧>.png`。
- 原型需要开发服务器。脚本会在 `PET_PROTO_PORT`（默认 5179）上自己启动 vite，结束时关闭。
  如果该端口已被别的服务占用，脚本报错退出，不复用它，请先停掉那个进程。

采样时刻是按时间估的，不是逐帧对齐。判断动作是否正确要看拼图，不要只看一帧。

## pet-interact.mjs：真实鼠标交互检查

```sh
node scripts/qa/pet/pet-interact.mjs
```

在 `pet.html` 上执行 9 项检查：抓取进入 `pickup`、拖动改变位置、松手回到 `idle`、戳击进入 `click_react`、
空白画布按下不抓取、睡觉时配饰仍戴着、关闭宠物后停止并隐藏。另外检查页面没有报错。

每项输出 `PASS` 或 `FAIL`。全部通过退出码为 0，否则为 1。

## 已知限制

- 这些脚本是无头浏览器检查，不能代替真机验证。抓屏排除、鼠标穿透（`WS_EX_TRANSPARENT`）、
  多屏和 DPI 行为只能在真实桌面窗口上验证，见 `PHASE3.md` §3.9。
- 对比图只能人工看。脚本不判断动作"对不对"，只检查交互状态和页面错误。
