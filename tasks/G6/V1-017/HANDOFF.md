# HANDOFF: V1-017（轮 45）

## 1. 我改了什么接口？

没有产品接口变更。用例侧的对外变化只有两处：

- `verify/cases/g5_cbr_probe.ps1`：`-Mode` 由 `ValidateSet(static|motion|both)` 改为**逗号列表**
  （`static` / `active` / `motion`，`both`=static+motion 保持旧语义，`all`=三者）。
  新增 `active` 场景；逐次汇总新增 `win5_spread` / `win5_ok` / `cap_ok` / `verdict_ok` / `capture_fps` 字段。
- 新增 `verify/lib/ActiveDesktop.ps1`（活动桌面激励源，`-Seconds` / `-Windowed` / `-BurstSec`）。

## 2. 下游需要接什么？

- **验收方**：跑 M3-04#1 时请用同一条命令（`-Mode static,active`）并要求两组都出数；
  `probe-summary.txt` 的 MODE 行可直接贴进报告。
- **下一轮**：`ActiveDesktop.ps1` 若被判定「强度不足」，需在卡里提高激励
  （建议方向：整屏视频回放作为活动源），不得改判据。

## 3. 已知限制是什么？

- `active` 激励强度弱于真实桌面（编码器真实输出 ~180KB/s vs 真实过冲秒 1.18MB）；
  因此 6/6 PASS **不能**外推为「真实桌面下成立」。
- 激励窗口是 `TopMost` 全屏窗体；若机器上有其它置顶窗口（如录屏工具条），可能被覆盖，
  此时 `capture_fps` 会掉到个位数——脚本会把它如实记进 `capture_fps` 字段，不会静默通过。

## 4. 哪些需求我没做完？

- M3-04#1 **本身没做完**（产品侧不达标，见 `tasks/G5/S4-005`）；本卡只负责把「静态/活动同脚本」
  与判据输出固化下来。
- 矩阵仍有 10 格未测（需 Win10 / 23H2 / 150% DPI / 三厂商硬编），本轮只补了 1 格，未补齐。
