# 验收 SOP · 技能标准版（已转为技能）

> **本文件已被 `.codebuddy/skills/acceptance-review/` 取代，保留仅作设计来源记录。**
> 运行时请使用技能 `/acceptance-review`，不要直接引用本文件。

## 为什么不是一个可直接用的 SKILL

`.codebuddy/plans/*.md` 只是工作区里的一个普通 Markdown 文件，CodeBuddy **不会**把它当技能加载：

| 机制 | 位置 | 是否能被 `/xxx` 调用 |
| --- | --- | --- |
| 技能 | `<项目根>/.codebuddy/skills/<name>/SKILL.md` 或 `~/.codebuddy/skills/<name>/SKILL.md` | ✅ 会 |
| 计划文件 | `.codebuddy/plans/*.md` | ❌ 不会 |

技能有硬性结构要求：目录内根级必须有 `SKILL.md`，其 frontmatter 必须含 `name` 与 `description`；`description` 决定 Agent 何时自动选用它。

## 现在的结构

```
.codebuddy/skills/
├── acceptance-review/          # 验收侧（规划 + 验收 Agent）
│   ├── SKILL.md                 # 入口：独立性硬约束、6 步流程、四档判定、findings 定级、评分
│   └── references/
│       ├── rerun-matrix.md      # 独立重跑清单：19 个用例 + 契约/覆盖工具 + 复跑策略
│       ├── findings-rubric.md   # 五轴↔A/C/E/K、严重度标签、remedy 词表、归因、偏差分型
│       └── report-template.md   # 验收报告模板 + 台账七列回写格式 + 下一轮指令模板
└── task-card-execution/         # 执行侧（任务执行 Agent）
    ├── SKILL.md                 # 入口：开工四项自查、6 步执行流程、四件套、Red Flags
    └── references/
        ├── delivery-contract.md # 四件套格式、证据三条硬要求、文件域白名单、覆盖模型
        └── skill-routing.md     # 两侧各用哪套通用技能 + 本项目的裁剪点
```

采用「入口 + references」的渐进式结构：`SKILL.md` 保持在可读的体量，判定表、模板、清单这类查阅性内容放在 `references/`，用到哪块再读哪块。

## 与已安装通用技能的关系

`~/.codebuddy/skills/` 下 25 个通用技能来自 `addyosmani/agent-skills`，提供工程方法论；项目级技能提供本项目的规范与判据。两者互补，冲突时以项目规范为准（详见 `task-card-execution/references/skill-routing.md` §3）。

## 维护约定

规范有变更时，**改技能，不改本文件**：

| 要改什么 | 改哪里 |
| --- | --- |
| 验收流程步骤 | `acceptance-review/SKILL.md` 的 The Process |
| 用例清单、期望值 | `acceptance-review/references/rerun-matrix.md` |
| 严重度、remedy、归因、评分公式 | `acceptance-review/references/findings-rubric.md` |
| 报告与台账格式 | `acceptance-review/references/report-template.md` |
| 执行流程、开工自查 | `task-card-execution/SKILL.md` |
| 四件套格式、文件域、覆盖模型 | `task-card-execution/references/delivery-contract.md` |
| 技能编排关系 | `task-card-execution/references/skill-routing.md` |

改完在 CodeBuddy 里 `/acceptance-review` 或 `/task-card-execution` 验证一次能否正常加载。
