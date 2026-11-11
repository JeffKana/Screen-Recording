# 技能编排：两侧各用哪套

> 已安装的通用技能来自 `addyosmani/agent-skills`（25 个），装在 `~/.codebuddy/skills/`。
> 项目级技能在 `.codebuddy/skills/`，只服务本项目的双 Agent 分工。
> 两者是**互补**关系：通用技能提供工程方法论，项目技能提供本项目的规范与判据。

## 1. 分工总表

| 侧 | 主技能 | 配套 | 调用的通用技能 |
| --- | --- | --- | --- |
| **规划 + 验收** | `acceptance-review` | — | `code-review-and-quality`、`security-and-hardening`、`performance-optimization`、`code-simplification` |
| **任务执行** | `task-card-execution` | — | `context-engineering` → `incremental-implementation` → `test-driven-development` →（可选）`code-simplification` / `source-driven-development` / `doubt-driven-development` |

## 2. 执行侧的调用顺序

```
拿到任务卡
  │
  ├─ 开工四项自查  ←── 任一不过，退回规划侧，不要自行解释
  │
  ├─ Step 1 读卡锁定范围
  │
  ├─ Step 2 装载上下文 ────────► context-engineering
  │     只读本轮要改的文件 + 直接依赖 + 对应判据
  │     不读整个 src/（越复杂越容易顺手改）
  │
  ├─ Step 3 薄切片实现 ────────► incremental-implementation
  │     一片 = 一条需求 = 一组可验证改动
  │     红线：不越文件域、不引依赖、不改 verify/
  │
  ├─ Step 4 先红后绿 ──────────► test-driven-development
  │     先在未修复的产物上跑红，再实现，再跑绿
  │     红-绿对照表进交付包
  │
  ├─ Step 5 交付前自检（DoD）
  │
  └─ Step 6 产出四件套
        EVIDENCE / SELFTEST / HANDOFF / artifacts
```

### 可选技能：什么时候加

| 情况 | 加哪个 | 位置 |
| --- | --- | --- |
| 改动大、边界不清、想不出怎么切 | `doubt-driven-development` | Step 3 之前，先质疑方案 |
| 涉及三方库/框架用法不确定 | `source-driven-development` | Step 3 内部，查官方文档再写 |
| 改动能跑但很难读、结构乱 | `code-simplification` | Step 5 之后、Step 6 之前 |
| 需要裁剪上下文（新会话、上下文太长） | `context-engineering` | 随时 |

### 不要加的情况

| 情况 | 为什么 |
| --- | --- |
| 任务卡小到一条断言 | `doubt-driven-development` 的成本大于收益 |
| 纯 UI 文案/样式微调 | 不值得跑完整 TDD 循环 |
| 需要跨文件域的改动 | 先提契约变更单，不是靠技能解决 |

## 3. 通用技能在本项目要做裁剪的地方

通用技能是通用的，本项目有更严的规范。**冲突时以项目规范为准。**

| 通用技能 | 通用做法 | 本项目的加严 |
| --- | --- | --- |
| `test-driven-development` | 红-绿-重构 | 红的证据是**旧产物上的实测值**，要进交付包对照表；项目已有「先红后绿」传统，比通用技能更严 |
| `incremental-implementation` | 薄切片 | 切片粒度由任务卡决定，**执行侧不得自行扩大范围** |
| `context-engineering` | 按需加载 | 加载清单以任务卡「必读章节」为准，不能自由发挥 |
| `code-review-and-quality` | 五轴审查 + 严重度标签 | 需求级用 PASS/PARTIAL/FAIL/BLOCKED 四档；finding 级用 Critical/Required/Optional/Nit/FYI；**每条 finding 必须带 remedy** |
| `security-and-hardening` | 通用 OWASP | 本项目额外受**依赖白名单与许可合规**约束（`vcpkg.json` 只读、FFmpeg `--disable-gpl`） |
| `performance-optimization` | 先测后优 | 本项目性能判据在 `docs/05` 有具体数值，必须实测对比，不能只给方向 |
| `doubt-driven-development` | 对抗式复核 | 本项目的复核是**独立重跑用例**，不是读代码挑毛病 |

## 4. 验收侧的调用顺序

```
收到执行报告 / 交付包
  │
  ├─ Step 1 读卡与需求原文  ←── 禁止先读执行报告
  │
  ├─ Step 2 先看测试 + 变异实验
  │     反转一个断言条件看是否转红；仍绿 = finding
  │
  ├─ Step 3 逐项判定 + 五轴审查
  │     PASS / PARTIAL / FAIL / BLOCKED
  │
  ├─ Step 4 定级 findings
  │     Critical / Required / Optional / Nit / FYI
  │     每条带：定位 + 现象 + 复现 + remedy + 归属
  │
  ├─ Step 5 验证验证（DoD 勾选 + 门判据对照）
  │
  └─ Step 6 结论回写与派发
        ├── docs/07 台账七列回写
        ├── docs/09 覆盖表回写
        ├── 评分 A/C/E/K + 单维红线检查
        ├── 归因（执行者 / 规划 / 环境）
        └── 下一轮指令（含「注意事项」节）
```

### 验收侧不该做的事

| 不该做 | 原因 |
| --- | --- |
| 采信执行报告的「通过」结论 | 报告是待验证的声明，不是证据 |
| 编写实现代码 | 验收权与执行权必须分离，否则闭环起点即失真 |
| 修改 `verify/` 下的用例 | 改尺子 |
| 自行下调验收要点 | 那是文档缺陷，提缺陷单走变更流程 |
| 替执行者解释需求 | 需求解释权在规划侧 |
| 在报告里给执行方派活但不写 remedy | 只说「这里复杂」等于没说 |

## 5. 两侧的隔离红线

| 红线 | 违反后果 |
| --- | --- |
| 执行方自评为「已验收」 | 闭环起点即失真 |
| 验收方替执行方实现 | 验收退化为自证 |
| 验收方修改被测代码或用例 | 本轮作废 |
| 任一方在结论里写「PASS」但证据不支持 | 台账不再是事实源，下一轮失忆 |
| 规划侧在验收报告里直接改验收要点 | 尺子被改，闭环失效 |
