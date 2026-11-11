# EVIDENCE: V1-012（`g5_format` 新判据两条断言）

> 执行方证据，非验收结论。数据 2026-11-11。
> 修改 `verify/` 用例是**验收方轮 44 指令 §1 第 2 点 / §2 P0 明确授权的判据落地**
> （把 `m3-04-cbr-spread` 拆成两条），不是「为让测试通过改尺子」：
> 判据数字 `10%` 与 `2×` 均来自裁定、一个未动，且新增两条反向护栏专防改软。

## 一条需求一条证据

M3-04#1 新判据（`docs/01:106`，轮 44）：
「CBR 模式下成片码率波动 ≤10%（5 秒滑窗口径），且单秒码率不超过标称的 2 倍」。

- 命令：`pwsh -File verify/cases/g5_format.ps1 -SimulateCbrRates <csv>`（不录制，验判据）
  以及 `pwsh -File verify/cases/g5_format.ps1`（真实录制，含两条新断言）。
- 两条断言（函数 `Assert-CbrWindow`，`g5_format.ps1`）：
  - `cbr-window5-spread`：`Get-Window5Stats` 在有效秒桶序列上取连续 5 桶、
    **逐秒滑 1 桶**，`spread=(max-min)/avg ≤ 0.10`，窗口数 0 判 FAIL。
  - `cbr-single-second-cap`：对**全部有效秒桶**判 `max ≤ 2 × 6000 = 12000kbps`，
    与滑窗断言并列执行，**不因滑窗 PASS 而跳过**。

### 滑窗口径（已写进用例头部注释，指令 §3 #2）

1. **桶**：沿用 `Get-PerSecondStats` —— ffprobe `v:0` 的 `packet.pts_time` 按
   `[int][Math]::Floor()` 落整秒桶，累加 `packet.size`；跳第 0 秒桶与最后一个桶；
   `bytes*8/1000` → kbps。
2. **滑法**：连续 5 桶一窗，**逐秒滑 1 桶**（第 1 窗 `[0..4]`、第 2 窗 `[1..5]`…），
   **不是 5 秒一跳**；共 `max(0, N-4)` 窗。
3. **窗口值**：5 桶算术平均码率（等价 5 秒总字节 / 5 秒），不是窗内 max/min。
4. **波动**：窗口值序列 `spread=(max-min)/avg`，沿用轮 32 定义，只换输入序列。
5. **单秒挡板**：全部有效桶逐个判 ≤2×，硬挡板，不许滑窗稀释（指令 §3 #3）。
6. 未动 `m3-04-cbr-nominal`（≤20%）、VBR 三条、60/15fps 断言与全部阈值字面量。

### 红-绿对照

| 新断言 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| `cbr-window5-spread` | 断言不存在；旧 `m3-04-cbr-spread` 在轮 43 十次定性里静态 4/5 超、运动 5/5 超（最高 359%） | 注入 20 桶（10×6000 + 10×5000）→ 16 窗、spread **18.2% FAIL**；正常数据见主卡 10 连测 |
| `cbr-single-second-cap` | 断言不存在；轮 43 出现 16,699kbps（2.8×）与 31Mbps 级 IDR 秒无人拦 | 注入 9 桶含 16,699 → 滑窗 **0.0% PASS 但硬挡板 FAIL**（`single_max=16699 > 12000`）；正常数据见主卡 |

- 真实录制的 10 次（静态 5 + 运动 5）结果见 `tasks/G6/V1-011/artifacts/r44-cbr10.txt`。

### 回归发现的一处遗漏（已修，留痕）

全量回归首轮 `g5_format` 报 `14 PASS / 1 FAIL`：
`FAIL m3-04-mode-isolation — 第二次 CBR：mode=cbr filler=3273014 spread=13.9%`。
根因是本卡只把 `m3-04-cbr-spread` 拆成两条，**漏了同文件尾部「VBR 后复跑 CBR」的
`m3-04-mode-isolation` 仍用旧逐秒 spread** → 同一需求出现两种判据。
修法：该断言改用同一套 `Get-Window5Stats`（5s 滑窗 ≤10% + 单秒 ≤12000 硬挡板），
判据数字一个未动，并在用例里写明原因。
复跑 `tasks/G6/V1-011/artifacts/rerun-g5-format.txt`：**15 PASS / 0 FAIL / 0 SKIP**
（`cbr-window5-spread` 3.4%、`cbr-single-second-cap` 7,022 vs 12,000、
`m3-04-mode-isolation` 滑窗 4.2%/6 窗、单秒 7,295）；`g5_format_selftest` 仍 19/19。

## 已知限制

- 本卡不声明需求子句；M3-04#1 状态由 `tasks/G6/V1-014` 独占。
- `-SimulateCbrRates` 是验收方要求的反向护栏注入入口；不传时行为与轮 43 一致
  （F2 的取数三档语义与全部判据不变）。
- 用例头部注释写明六条口径，避免下一个人按另一种滑法重算。
- 执行方不自评为已验收。