# TASK: V1-023 轮 51 · rc 之后的发布复核与 GA 前置（不加功能）

阶段：G6｜角色：V1（主卡：复核 / 文档 / 用例记录 / 交付包）｜轮次：51
来源：`.codebuddy/plans/轮51指令.md`（判据以此为准）+ `docs/23`（rc 定位）+ `docs/22`（差距表）

> 本轮**不加功能、不改判据、不改 `verify/` 下的断言**。改动面只有：文档、台账、issue 模板、交付包。
> 上游：`main` @ `4b117cf`（rc.1 已发布 `docs/24`）。build 复用 `build-msvc`（本轮 `src/` 零改动），
> 但 ER/LT 全部在**同一个 exe** 上跑并记 SHA256（见 `EVIDENCE.md §0`）。

## 需求范围

| # | 项 | 要求（照抄轮 51 卡） | 证据形式 |
| --- | --- | --- | --- |
| P0-1a | ER-04 磁盘耗尽 ×3 | 真跑三轮、独立目录；`disk_stop=1`、可播、无 `.part`、无 0 字节 | `g5_inject` 的 `disk-run1..3` + 本轮补的独立目录 3 次（`EVIDENCE.md §1`） |
| P0-1b | ER-02 强杀 ×3 时机 | 录制中/暂停中/收尾前各 1；恢复 ≥ 原时长−10s、含音轨、`repairable=1` | `g5_inject` 的 `kill-*`（每时机 ×3）+ 9 万毫秒级长样本（`EVIDENCE.md §2`） |
| P0-1c | ER-01 代理标注 | 报告第一行写「ER-02 强杀代理，非物理断电」 | `EVIDENCE.md §2` 首行 |
| P0-1d | 缺陷分级重列 | 轮 26–50 全部 findings 按 `05 §7.1` 分级：编号/级别/闭环/证据；阻塞 0、严重 ≤3 有计划 | `docs/26-轮51-缺陷分级重列.md` |
| P0-2e | LT-01 第 2 次 | 8h 单文件（28,800s），六项判据同轮 35 口径；独占窗口、不挂 UI | `verify/artifacts/lt01-real-*/summary.json`（本轮启动时间与状态见 `EVIDENCE.md §4`） |
| P0-2f | LT-02 第 2 次 | 30min 静音段 + 恢复段无爆音，8/8 | `verify/artifacts/lt02-*/`（`EVIDENCE.md §4`） |
| P0-3g | issue 模板 | `.github/ISSUE_TEMPLATE/bug_report.yml`：必填「构建 SHA / 诊断包 / 复现步骤 / 是否 MP4」 | 模板 + YAML/结构自检输出（`EVIDENCE.md §6`） |
| P0-3h | `docs/23` 分级节 | 补一节：rc 期缺陷如何分级、什么条件发 rc.2 | `docs/23 §8` |
| P1-i | README | 补「rc 版本升级请先 uninstall 再 install」 | `README.md` 安装与升级段 |
| P1-j | 真机需求表 | M6-01/04/05/08 各自缺什么机器、谁能提供、到位后首轮做什么 | `docs/25-轮51-真机需求清单.md` |
| P1-k | 台账 | 轮 51 一行留白 | `docs/07` 主表轮 51 行 + 「轮 51 记录」 |

## 必读章节（开工四项自查见 `SELFTEST.md §0`）

- `docs/23`（rc 定位 / rc 门禁 / 转 GA 五条）、`docs/24`（rc.1 发布记录）
- `.codebuddy/plans/轮51指令.md`（本卡全文）
- `docs/22`（六条判据差距）、`docs/05 §7.1/§7.2`（分级与发布判据）、`docs/19 §0.A`（四条真机）
- `docs/06`（角色/四件套/红-绿）、`docs/20`（分支/PR/CI/一行制）
- `CONTRACT.md §7`（管道协议）、`§18.2`（退出码表，含 `exit 2`）
- `.codebuddy/plans/验收报告-轮49.md`（R1/R2/R3 三条遗留）、`tasks/G6/S4-007/EVIDENCE.md`

## 文件域白名单

| 类别 | 文件 |
| --- | --- |
| 文档 | `docs/07`、`docs/09`（生成物，若需重生成）、`docs/23`、`docs/24`、`docs/25`（新）、`docs/26`（新）、`docs/INDEX.md` |
| 仓库基建 | `.github/ISSUE_TEMPLATE/bug_report.yml`（新）、`README.md`（安装段） |
| 交付包 | `tasks/G6/V1-023/**`（`artifacts/` 不入库） |

**禁止**：`src/**`（本轮零代码改动）、`verify/cases/**` 与 `verify/tools/**` 的断言、
`docs/01`（需求原文）、`docs/05`（判据）、`vcpkg.json`、他人任务卡。

## 覆盖子句

无（本轮是发布复核与 GA 前置，不新增/翻绿任何需求子句；覆盖表应保持 **111 / 1 / 4**）。

## 交付状态

不声明子句完成。P0/P1 逐条结果见 `EVIDENCE.md`；**未做的项在 `HANDOFF.md §3` 逐条写明「未做/未测」**。
本轮不自评「已验收」：ER/LT 的原始输出与 SHA256 由验收方独立重跑核对。
