# 桌宠美术脚本

重绘或换一套桌宠画风时使用。完整流程、约束与经验见 [`docs/pet-art-pipeline.md`](../../docs/pet-art-pipeline.md)。

| 脚本 | 作用 |
| --- | --- |
| `grid_from_shapes.py` | 形状配方（`recipes/`）→ 网格，打印 ASCII 预览 |
| `apply_grid.py` | 把网格写进 `sprite.json` 的 head、body、eyes、mouth 四段，默认只显示差异 |
| `refit_accessories.py` | 报告头、身体、眼睛的位置，按位移重新对位配饰 |
| `sync_sprites.py` | 运行渲染、核对 sheet 尺寸、与 `data/pet/sprites` 比对；`--apply` 复制 |
| `compose_check.py` | 一帧加表情与每件配饰，输出审查图 |

都从仓库根目录用 `python -I` 运行，需要 Pillow。会改文件的脚本默认只预览，加 `--write` 或 `--apply` 才写入。
