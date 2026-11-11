# EVIDENCE: T-17 可观测性（三项指标 + 诊断包 + 日志分级）

> 轮 30 · 证据均为本轮实测（MSVC Release + MinGW 双构建，2026-10-23）。

## 1. M6-03 三项指标

| 子句 | 证据 | 结果 |
| --- | --- | --- |
| #1 录制中可查看三项实时指标 | `g5_diag` A 段：recorder `metrics capture_fps=… encode_fps=… dropped=…` 5 次采样，间隔 1015/1000/1000/1000ms；UI 状态栏文本「录制中 00:03 · 剩余可录 … · 全屏 / 采集 32.0 / 编码 30.0 fps · 丢帧 0 · sr-demo.mkv」 | PASS（1Hz） |
| #2 丢帧计数 + 日志记录 | `--inject-drop-every 10` 注入：`dropped=15`、`frames_dropped=17`、日志出现 `dropped=[1-9]`；UI 日志 `snapshot state=Recording … capture_fps=32.00 encode_fps=30.00 dropped=0`（值来自快照） | PASS |

- 指标来源：`RecordingSession` 主循环内计数（采集到达 / 编码提交 / 丢帧），快照扩展块 `ext_bytes + ext_version + capture_fps_x100 + encode_fps_x100 + dropped + captured` 下发；
  UI 只做 `/100` 还原与排版，**无任何帧数÷时间推算**（代码位置：`src/app/main.cpp` `FormatMetrics`）。
- 截图：`verify/manual/round30-metrics.png`。

## 2. M8-14 诊断包

| 子句 | 证据 | 结果 |
| --- | --- | --- |
| #1 一键导出含版本/硬件/日志/配置 | `sr-ui --export-diag` 与 `sr-recorder --export-diag` 两个入口各产出 `version.txt` / `gpu.txt` / `log.txt` / `config.ini`；`diag_pack ok=1 files=4 bytes=1605 ms=0`；托盘菜单入口截图 `round30-diag-menu.png` | PASS |
| #2 日志分级可调且不含画面 | debug/info/warn 三次录制行数 92 / 2 / 0；INI `[log] level=debug` 与 `--log-level warn` 均生效；诊断包内无 PNG/JFIF/ftyp/ID3/RIFF 签名、用户名脱敏命中 0 | PASS |

- 时间/体积：1605 字节、0ms（预算 5MB / 2s）；log.txt 走 4096 行环形缓冲 + 尾部截断，不整文件拷贝。
- 脱敏：`C:\Users\<name>` → `C:\Users\<user>`（`SanitizeText`，对 log/config 全量应用）。

## 3. 覆盖表三态 + C8（轮 29 F1 收口）

- 回扫 G1 两卡：`S1-001` M2-16 = 已实现；`S1-002` M8-14 = **有卡未实现**（骨架），本轮由 T-17 卡转「已实现」。
- 模型：`coverage_model.card_delivery()` 读 `## 交付状态`；`build_rows()` 输出三态；C8 对「有卡未实现 + 无卡」都要求挂账。
- 自检：`coverage_report.py --self-test` → `self_test ok`（构造 stub 卡证明「有卡未实现」不会被算成已实现）。
- 现状：116 子句 / 已实现 75 / 有卡未实现 0 / 无卡 41 / 挂账 41；`contract_check` **8/8**，覆盖表 up to date。

## 4. 脚本清理统一 + 开关隔离（轮 29 F2/F3）

- `verify/lib/Cleanup.ps1`：`Register-SrProcess` / `Stop-SrProcess` / `Clear-SrProcesses -IncludeNameFallback` / `Read-TextSafe`（FileShare.ReadWrite）。
- 13 个用例脚本全部 dot-source + 本地一行 trap + spawn 处登记；`ui_smoke` 的 3 处 `File.ReadAllText` 改为 `Read-TextSafe`。
- 人为异常验证：trap 打印 `FAIL: induced failure` 后 `sr-ui` 残留 = 0（`g5_diag` E 段）。
- `SR_TEST_HOOKS`：`--preflight-fail` / `--set-*` / `--fail-*` / `--inject-drop-every` 在 `--help` 里单列「调试开关（Release 交付包不含）」；`--version` 打印 `test_hooks=1/0`。

## 5. 回归（全绿）

| 用例 | 结果 |
| --- | --- |
| `g5_diag`（新） | 36 / 0 / 0 |
| `g4_gate`（60s） | 12 / 0：spawn→Recording 485ms、preflight 93ms、stop→playable 97ms、60.199s / 1807 帧、重复帧 0 |
| `ui_smoke` | 101 / 0 / 0 |
| `g4_channel` / `g4_control` / `g4_elapsed` | 52 / 32 / 13 |
| `g5_inject` / `g5_fallback` | 28 / 25 |
| `g3_output` / `g3_playback` / `g3_recovery` / `g3_storage` | 15 / 11 / 9 / 11 |
| `g1_walk` / `g2_walk` | 40 / 14（3 SKIP：WGC-MinGW 与空闲会话吞吐，非本轮改动） |
| `contract_check` / `coverage_report --check` | 8/8；up to date |
| 双构建 | MSVC Release 与 MinGW 均零 error / 零 warning |
| 残留进程 | 0 |

## 6. 未达成 / 保持 BLOCKED

- **M6-06（150% 混合 DPI）仍 BLOCKED**：本机双屏均 100%，无法构造。交付说明不得写「DPI 已验证」。
- drain 优化（崩溃丢失 3s→2s）按指令本轮不做。
- 「最近 10 分钟日志」在独立进程导出时以环形缓冲为主、日志文件尾部兜底（有界读取 128KB），不做整文件拷贝。
