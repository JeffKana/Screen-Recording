# SELFTEST: G5/S5-002（M5-06 列表）

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01-功能需求清单.md:171` M5-06 原文「列表按时间倒序；录制中的条目实时更新时长；异常中断的条目标注「可恢复」」 |
| 必读章节可定位 | 通过 | `docs/02` §4 UI/Recorder 边界；`docs/01` M5-04 |
| 上游交付物可获取 | 通过 | `MkvRecovery::Scan/Probe`（`--scan` 输出）、`OutputPath::CommitOutput` 的 `.part` 命名 |
| 文件域无交集 | 通过 | 本卡碰 `src/app`、`src/recorder/main.cpp`、`src/resilience`；S1-005 碰 `src/encode` 与 `RecordingSession.*` |

## 1. 自测命令与输出

| 命令 | 结果 | 时间戳 |
| --- | --- | --- |
| `sr-recorder --list <dir>` | count=5，倒序，含可恢复/不可恢复 | 2026-10-26 11:50 |
| `sr-recorder --scan <dir>` | repairable=1 / 0 与列表一致 | 2026-10-26 11:50 |
| `pwsh -File verify/cases/g5_list.ps1` | **24 PASS / 0 FAIL** | 2026-10-26 11:54 |
| `pwsh -File verify/cases/ui_smoke.ps1` | 101 PASS / 0 FAIL（新按钮不影响 A-J 段） | 2026-10-26 11:59 |
| `python verify/tools/contract_check.py` | 8 / 8 | 2026-10-26 12:30 |

## 2. 红-绿对照

| 新断言 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| `sr-recorder --list` | 参数不存在 → usage + exit 1 | `count=5` + 5 条 `entry=...` |
| `list-desc-order` | 无列表 UI | 与 mtime 倒序完全一致 |
| `list-recoverable-matches-scan` | 无「可恢复」概念 | list=[crash.mkv.part] == scan=[crash.mkv.part] |
| `list-live-updates` | 无列表 UI | 4 次采样 34ms → 3,082ms |
| `list-duration-*`（解析正确性） | 首次实现把行尾 `\r` 带进 name，`name=a.mkv\r` 查不到（24 项里 19 FAIL） | 剪掉 `\r` 后 24/24，duration 与 ffprobe 差 ≤1ms |

## 3. 回归

| 用例 | 结果 |
| --- | --- |
| `g5_list` | 24 / 24 |
| `ui_smoke` | 101 / 101 |
| `g4_channel` / `g4_control` / `g4_elapsed` / `g4_gate` | 52 / 32 / 13 / 12 |
| `g3_output / playback / recovery / storage` | 15 / 11 / 9 / 11 |
| `g5_fallback` / `g5_inject` / `g5_diag` | 25 / 28 / 36 |
| `g1_walk` / `g2_walk` | 40 / 40；14 + 2 SKIP |
| 契约 / 覆盖表 | 8 / 8；75 / 9 / 32 |

## 4. DoD 勾选

- [x] 每条子句一条证据（#1/#2/#3 分开列）
- [x] 新断言先红后绿（`--list` 不存在 → 存在；`\r` 缺陷 → 修）
- [x] Release 零告警
- [x] 回归零失败
- [x] 无调试残留（`--show-list` 在 `SR_TEST_HOOKS` 下，发布构建不含）
- [x] 未超出文件域
- [x] 无接口变更（`--list` 是新增 CLI，不改既有输出）
- [x] 文档同步（docs/09 M5-06 三子句已实现；docs/07 轮 32）

## 5. 已知限制

- 可恢复 `.part` 的探测是**一次性全量读**（`Probe` 现有实现）。崩溃残留通常几 GB，
  打开列表时有一次秒级停顿；正在录制的 `.part` 已用 `--list-skip` 跳过，不会卡 UI。
  彻底改为「尾部窗口扫描」需要改 `MkvRecovery` 的解析入口，留给 M8-09（文件损坏检测）一起做。
- 列表刷新间隔：非录制约 3s、录制中 5s；「录制中」那一行的时长是 1s 刷新（取实时快照）。
- 未做双击打开/右键菜单（属 M5-07/M5-08）。
