# TASK: V1-025 轮 53 · 0.1 MVP 发布收尾

阶段：G6｜角色：V1（主卡：文档回写 / 出包 / 冒烟 / 发布）｜轮次：53
来源：`.codebuddy/plans/轮53指令.md`（判据以此为准）+ `docs/27`（0.1 范围冻结与例外裁定）
+ `RELEASE-NOTES-0.1.0.md`（发布说明草稿）+ `验收报告-轮51`（裁定 R1~R4 的落地）

> 本轮**不发新功能**：文档口径统一 + `build_release` 的 `channel` 三态 + 出 0.1.0 包 + 干净目录冒烟 + GitHub release。
> **`ga_ready=0` 必须保留**；标题与正文必须带「仅本机验证」；例外（EX-02~06）只写「未测」，不得写「兼容/已验证」。
> 上游：`round/52`（faststart 预留回填 + 长稳第 3 次启动）——**本分支基于 `round/52`**，
> 待 O0 按 SOP 先合 `round/52` 再合本分支（合并顺序见 `HANDOFF.md §4`）。

## 需求范围

| # | 项 | 要求（照抄轮 53 卡） | 证据形式 |
| --- | --- | --- | --- |
| P0-1 | 文档回写 0.1 口径 | `docs/22` ⑤⑥ 与 4 条真机 →「1.0 前置 / 0.1 例外」、① 行列出 5 条例外；`docs/09` 4 条 + `M1-27#1` 的理由改「0.1 范围外（docs/27 §3）」；`docs/23 §4` 改「转 1.0-GA 的五条硬条件」；`docs/24` rc.1 行备注「已被 0.1 取代」+ 新增 0.1.0 行；README 横幅 rc → 0.1 | `EVIDENCE.md §1`；`coverage_report --check` 仍 PASS（改理由不改状态） |
| P0-2 | 出 0.1.0 包 | `build_release.ps1 -Version 0.1.0` 的 `channel` 改三态（`-rc.`→rc / `0.1.0`→mvp / 其余→ga）；`VERSION.txt` = `version=0.1.0 / channel=mvp / ga_ready=0`；17 项自检全过；记录 zip/exe SHA256 与 `source_fingerprint` | `EVIDENCE.md §2` |
| P0-3 | 干净目录冒烟 | 解压 → `install.ps1` → 录 60s（默认参数）→ `uninstall.ps1`；① 成片可播 ② 无残留目录 ③ 用户录制文件未被删 | `EVIDENCE.md §3` |
| P0-4 | 发布 | `gh release create v0.1.0 <zip> --title "0.1（MVP，仅本机验证）" --notes-file RELEASE-NOTES-0.1.0.md`（**不加 `--prerelease`**）；tag `v0.1.0`；`docs/24` 补行；发布后更新 `docs/22`「当前」列 | `EVIDENCE.md §4` |

## 必读章节（开工四项自查见 `SELFTEST.md §0`）

- `docs/27`（0.1 范围冻结：§2 两级发布模型、§3 例外 EX-02~06、§4 六条门槛、§6 三条禁止）
- `docs/26`（缺陷分级现状）、`docs/22`（六条判据差距）、`docs/23 §4/§5/§7`（1.0 前置与四段说明）
- `.codebuddy/plans/轮53指令.md`（本卡全文）、`RELEASE-NOTES-0.1.0.md`
- `docs/20`（分支 / PR / CI / 一行制）、`docs/05 §7.1/§7.2`、`CONTRACT.md §7`/`§18.2`（未改动）

## 文件域白名单

| 类别 | 文件 |
| --- | --- |
| 文档 | `docs/07`、`docs/09`（只改最后一列理由）、`docs/22`、`docs/23`、`docs/24`、`README.md`、`RELEASE-NOTES-0.1.0.md` |
| 工具 | `verify/tools/build_release.ps1`（只改 `VERSION.txt` 的 channel/ga_ready 三态） |
| 交付包 | `tasks/G6/V1-025/**`（`artifacts/` 不入库） |

**禁止**：`src/**`（本轮零代码改动）、`docs/01` / `docs/05` / `docs/27`（裁定原文）、`verify/cases/**` 的判据、
`vcpkg.json`、把例外写成「兼容/已验证」、把 `ga_ready` 改成 1。

## 覆盖子句

无（本轮是发布口径与出包，不新增/翻绿/回退任何子句；`coverage_report --check` PASS、状态保持 **111 / 1 / 4** 即为证据）。

## 交付状态

不声明子句完成。P0 逐条结果见 `EVIDENCE.md`；**未做的项在 `HANDOFF.md §3` 逐条写明**。
本轮不自评「已验收」；发布物的 SHA256 由验收方独立核对。
