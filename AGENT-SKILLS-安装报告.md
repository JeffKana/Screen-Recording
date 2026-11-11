# addyosmani/agent-skills 安装报告

- **来源仓库**：https://github.com/addyosmani/agent-skills （MIT）
- **安装时间**：2026-10-26
- **安装位置**：用户级 `C:\Users\98468\.codebuddy\skills\`
- **技能数量**：25 个（另含仓库级共享资源）
- **加载状态**：已通过运行时实测加载成功，**无需重启 IDE**

> 本文件与本项目业务代码无关，仅为本次技能安装的交付记录，可随时删除。

## 一、安装内容

### 1. 技能（25 个）→ `~/.codebuddy/skills/<name>/SKILL.md`

每个目录均含 `SKILL.md`（YAML frontmatter 含 `name` + `description`），CodeBuddy 可自动发现并按 description 自动选用，也可 `/<name>` 显式调用。

| 阶段 | 技能 |
|------|------|
| Meta | `using-agent-skills` |
| Define | `interview-me`、`idea-refine`、`spec-driven-development`、`constraint-driven-development` |
| Plan | `planning-and-task-breakdown` |
| Build | `incremental-implementation`、`test-driven-development`、`context-engineering`、`source-driven-development`、`doubt-driven-development`、`frontend-ui-engineering`、`api-and-interface-design` |
| Verify | `browser-testing-with-devtools`、`debugging-and-error-recovery` |
| Review | `code-review-and-quality`、`code-simplification`、`security-and-hardening`、`performance-optimization` |
| Ship | `git-workflow-and-versioning`、`ci-cd-and-automation`、`deprecation-and-migration`、`documentation-and-adrs`、`observability-and-instrumentation`、`shipping-and-launch` |

### 2. 仓库级共享资源（关键补丁）

仓库把 7 份共享检查清单放在仓库根 `references/`，技能内部以 `../../references/xxx.md` 相对路径引用。若只复制 `skills/`，这些引用会全部断链。因此额外复制：

| 路径 | 内容 | 作用 |
|------|------|------|
| `~/.codebuddy/references/` | 7 份清单：`definition-of-done`、`testing-patterns`、`security-checklist`、`performance-checklist`、`accessibility-checklist`、`observability-checklist`、`orchestration-patterns` | 使 `../../references/*.md` 相对路径正确解析（`~/.codebuddy/skills/<name>/` 的 `../..` 即 `~/.codebuddy/`） |
| `~/.codebuddy/agents/` | 4 个角色卡：`code-reviewer`、`test-engineer`、`security-auditor`、`web-performance-auditor` | `doubt-driven-development` 中引用的评审角色清单 |
| `~/.codebuddy/skills/doubt-driven-development/agents/` | 同上 4 个角色卡 | 供该技能内文中的相对路径 `agents/` 直接命中 |

技能自带的子目录（`constraint-driven-development/references`、`performance-optimization/references`、`security-and-hardening/references`、`idea-refine/scripts` 等）已随技能目录一并安装。

## 二、校验结果

| 校验项 | 结果 |
|--------|------|
| 25 个技能 `SKILL.md` 存在 | 通过 |
| YAML frontmatter 起始于 `---` | 通过 |
| `name` 字段存在 | 通过 |
| `description` 字段存在 | 通过 |
| `name` 与目录名一致 | 通过（25/25） |
| 技能内 `../../references/*.md` 相对引用可解析 | 通过（24 条有效引用，0 条缺失） |
| 运行时加载 | 通过（`using-agent-skills` 实测返回 base directory） |

## 三、使用方式

**显式调用**（聊天输入框）：

```
/spec-driven-development
/code-review-and-quality
/test-driven-development
```

**自动调用**：CodeBuddy 依据各技能 `description` 与当前任务匹配后自动选用。

**查看全部**：`/skills`

**推荐入口**：不确定用哪个时先走 `/using-agent-skills`，它内置完整的阶段路由图与「快速参考表」。

典型全流程链路：

```
interview-me → idea-refine → spec-driven-development → planning-and-task-breakdown
→ incremental-implementation → test-driven-development → code-review-and-quality
→ code-simplification → shipping-and-launch
```

## 四、注意事项

1. **仓库 `commands/` 未安装**。该目录是 Antigravity（`.toml`）与 Claude Code（`.claude/commands/*.md`）的命令包装层，与 CodeBuddy 的技能机制不兼容；CodeBuddy 中直接用 `/<技能名>` 即可，功能等价。
2. **`browser-testing-with-devtools` 依赖 Chrome DevTools MCP**，当前环境未配置该 MCP，该技能的实际执行会受限，但流程规范本身可用。
3. **`doubt-driven-development` 的角色卡面向 Claude Code 的 subagent 隔离上下文机制**，在 CodeBuddy 中可直接参考角色卡提示词手工使用。
4. **更新**：临时克隆位于 `%TEMP%\agent-skills-src`（浅克隆）。重新执行 `git -C $env:TEMP\agent-skills-src pull` 后，重跑本次的复制步骤即可覆盖升级。
5. **无关但已发现的问题**：既有技能 `androidopenclaw` 的 `SKILL.md` frontmatter 存在转义错误（写成 `\#` / `##` 而非 `#`），会导致其 `name`/`description` 无法被解析。该问题为安装前既有问题，本次未改动。
