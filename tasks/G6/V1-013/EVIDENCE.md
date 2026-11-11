# EVIDENCE: V1-013（反向护栏 ×2，self-test 17 → 19）

> 执行方证据，非验收结论。数据 2026-11-11。
> 命令：`pwsh -File verify/cases/g5_format_selftest.ps1`
> 输出：`tasks/G6/V1-013/artifacts/selftest-19.txt` — **19 PASS / 0 FAIL**，退出码 0。

## 一条需求一条证据

判据（轮 44 指令 §2 P0）：造「滑窗过 10%」「单秒过 2×」各一条，各自必须 FAIL。

| 断言 | 注入序列 | 实测输出 | 退出码 |
| --- | --- | --- | ---: |
| `r44-window-spread-fail` | 20 桶 = 10×6000 + 10×5000 | `FAIL cbr-window5-spread — 5s 滑窗（逐秒滑 1 桶，共 16 窗）5,000~6,000kbps avg=5,500 spread=18.2%（判据 ≤10%）` + `PASS cbr-single-second-cap — 单秒峰值 6,000 vs 硬上限 12,000`；`RESULT: 1 PASS / 1 FAIL / 0 SKIP` | 1 |
| `r44-single-second-cap-fail` | 9 桶 = 4×6000 + **16699** + 4×6000 | `PASS cbr-window5-spread — 共 5 窗 8,140~8,140 spread=0.0%` + `FAIL cbr-single-second-cap — 单秒峰值 16,699 vs 硬上限 12,000`；`RESULT: 1 PASS / 1 FAIL / 0 SKIP` | 1 |

- 第二条是**关键**：9 个桶恰好产生 5 个滑窗，每个窗口都含那个尖峰 →
  窗口值全等、spread **0.0% PASS**。若没有单秒 2× 硬挡板，这次真实失控值
  （轮 43 实测 16,699kbps = 2.8× 标称）就会被滑窗完全稀释掉。
  这条护栏直接兑现指令 §3 #3「不许以滑窗会稀释为由放过」。
- 两条都断言 `SKIP=0`：异常数据必须判 FAIL，不得藏成「数据未取到」。
- 原 **17 条一条未删**；`f2-threshold-spread-10pct` 守卫的模式因断言改名同步更新为
  `$w\.spread -le 0\.10`，**仍强制文本含判据数字 `-le 0.10`**（没被改软）。
- 红-绿对照：旧 self-test 只有 17 条、无这两条护栏 → 新 19 条，
  注入异常时用例从「可能全绿」变为「精确 FAIL 对应的那一条」。

## 已知限制
- 注入的是伪造逐秒序列，不启动 recorder/ffplay；验的是判据逻辑，不代替真实录制。
- 本卡不声明需求子句；`tasks/G6/V1-013/artifacts/` 存两条护栏的原始输出。
- 执行方不自评为已验收。