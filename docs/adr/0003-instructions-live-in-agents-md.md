# 0003 项目指令的唯一出处是 AGENTS.md

- **状态**：accepted
- **背景**：不同编码工具读不同名字的项目指令文件。两处各写一份，改一处漏一处就开始漂——真发生过：
  原型清单在 `AGENTS.md` 里漏了一次。
- **决定**：`AGENTS.md` 是唯一活的项目指令；`CLAUDE.md` 只留一行 `@AGENTS.md`（Claude Code 启动时展开
  导入），标题与正文都不在那里。
- **后果**：改文档一律改 `AGENTS.md`，不改 `CLAUDE.md`。pre-commit 的原型清单检查因此比对的也是
  `AGENTS.md`。
