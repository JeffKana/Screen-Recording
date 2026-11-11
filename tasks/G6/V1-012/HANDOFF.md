# HANDOFF: V1-012（新判据两条断言）

## 一、我改了什么接口？
无接口变更。只改 `verify/cases/g5_format.ps1`：
- 头部注释新增「滑窗口径」六条；
- 新增 `Get-Window5Stats`（5 桶滑 1 桶）与 `Assert-CbrWindow`（两条并列断言）；
- `m3-04-cbr-spread` → `cbr-window5-spread` + `cbr-single-second-cap`；
- `m3-04-mode-isolation`（VBR 后复跑 CBR）由旧逐秒 spread 同步改为同一套
  「5s 滑窗 + 2× 挡板」——全量回归首轮报 13.9% FAIL 才发现的遗漏；
- 新增 `-SimulateCbrRates <csv>`（反向护栏注入，不录不启 ffplay）；
- F2 的取数三档语义、其余全部阈值与断言一字未动。

## 二、下游需要接什么？
1. 验收方独立重跑 `pwsh -File verify/cases/g5_format_selftest.ps1`（19/19）与
   `pwsh -File verify/cases/g5_format.ps1`（真实录制两条断言）。
2. 重算时**必须按用例头部六条口径**：逐秒滑 1 桶、5 桶算术平均、跳首尾桶；
   按「5 秒一跳」或「重新按 5 秒分桶」会得到不同数值。
3. 单秒 2× 是硬挡板；若未来有人想用「滑窗 PASS」跳过它，两条反向护栏会拦下。

## 三、已知限制是什么？
- 判据窗口是裁定值，不是执行方自取；本卡无权改 10% / 2× / 窗口宽度 5。
- 模拟注入只验判据逻辑，不代替真实录制；真实 10 连测在主卡。
- 本轮不改码控（`src/encode/`），V1.5 的 IDR 预算 / HRV buffer 已登记在 `docs/17 §7`。

## 四、哪些需求我没做完？
| 项 | 状态 | 原因 / 下一步 |
| --- | --- | --- |
| 两条新断言 + 滑窗注释 | 已交付 | 19/19 自检含两条反向护栏 |
| 10 次全过与翻绿 | 不在本卡 | 归 V1-011 / V1-014 |
| 码控层改进 | **没做（也不该做）** | 指令 §3 #1：V1.5 范围 |
| V1.0 发布 | 仍不可发布 | 见主卡 HANDOFF |

## 五、交付包清单
`tasks/G6/V1-012/`：TASK.md、EVIDENCE.md、SELFTEST.md、HANDOFF.md。

**执行方不自评为已验收。**