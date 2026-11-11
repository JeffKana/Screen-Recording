# EVIDENCE: S5-004（存储守护收口 M8-03#1 / M8-03#2 / M8-04#1 / M8-05#1）

## 0. 结论

| 子句 | 结论 | 证据 | 实测要点 |
| --- | --- | --- | --- |
| M8-03#1 | PASS | E1 | disk_warn=1、disk_stop=0、record_exit=0、wall=4.03s、产物 3.1MB；UI 状态栏出现磁盘提示 |
| M8-03#2 | PASS | E2 | disk_critical=1、状态栏「录制将随时停止！」；反向对照不误报 |
| M8-04#1 | PASS | E3 | disk_stop=1、wall=2.08s（请求 30s）、产物 2.1MB、无 .part、容器合法 |
| M8-05#1 | PASS | E4 | 估算 29,082s vs 实测口径 28,947s，相对误差 0.5%（判据 ≤20%） |

## 1. 唯一证据来源与可重跑命令

```
pwsh -NoProfile -File verify/cases/g5_storage.ps1 -BuildDir build-msvc -Config Release
```

本轮实跑输出（2026-11-08，build-msvc / Release）：

```
--- M8-03#1 剩余不足时非阻塞提示、录制继续 ---
  PASS m8-03-01-warn-fired — disk_warn=1（提示线触发）
  PASS m8-03-01-not-blocking — disk_stop=0 record_exit=0（提示不阻断，录制继续）
  PASS m8-03-01-recording-continued — wall=4.03s size=3101326
  PASS m8-03-01-ui-warn-logged — UI 日志记录了非阻塞提示
  PASS m8-03-01-ui-warn-visible — 状态栏首行：⚠ 磁盘剩余不足 999999999MB  就绪。输出目录: ...
--- M8-03#2 剩余不足 500MB 升级为强提示 ---
  PASS m8-03-02-critical-logged — UI 日志记录了强提示
  PASS m8-03-02-critical-visible — 状态栏首行：⚠ 磁盘剩余不足 999999999MB，录制将随时停止！  就绪。...
  PASS m8-03-02-no-false-critical — 空间充足时不误报强提示
  PASS m8-03-02-no-false-warn — 空间充足时不误报非阻塞提示
--- M8-04#1 空间耗尽前自动停止、成品完整 ---
  PASS m8-04-01-stopped-early — disk_stop=1 wall=2.08s（请求 30s）
  PASS m8-04-01-output-complete — size=2110041 part_left=False
  PASS m8-04-01-container-valid — RESULT: 0 failure(s)
--- M8-05#1 可录时长估算误差 <= 20% ---
  PASS m8-05-01-estimate-error-20pct — 估算=29,082s 实测口径=28,947s 误差=0.5%（实测码率=777,627 B/s, free=22,643,912,704 B, bytes_written=7,791,827）

RESULT: 13 PASS / 0 FAIL / 0 SKIP
```

## 2. 一条子句一条证据

### E1 · M8-03#1（剩余 < 2GB 时非阻塞提示、录制继续）

- **判据**：剩余不足提示线 → 提示但**不阻断**，录制继续到结束且产物落盘。
- **实测**：`disk_warn=1`（提示线触发）、`disk_stop=0`、`record_exit=0`、`wall=4.03s`、
  产物 3,101,326 字节。UI 侧日志 `disk_warn=1 free_mb=... threshold_mb=2048`，
  状态栏首行出现「⚠ 磁盘剩余不足 ...MB」。
- **可重跑**：同上命令（断言 `m8-03-01-*`）。

### E2 · M8-03#2（剩余 < 500MB 升级为强提示）

- **判据**：低于强提示线时提示**升级**（与普通提示可区分），空间充足时不误报。
- **实测**：日志 `disk_critical=1`；状态栏首行「⚠ 磁盘剩余不足 ...MB，**录制将随时停止！**」
  （与 E1 的普通提示文案不同）。反向对照：`disk_critical_mb=1` / `disk_warn_mb=1` 时
  日志**没有** `disk_critical=1`，也**没有** `disk_warn=1` → 不误报。
- **实现落点**：`src/app/main.cpp` 的 `RefreshDiskRemaining`（按 `disk_warn_mb` /
  `disk_critical_mb` 判级，跨阈值时记一行）+ `UpdateUi`（强提示放在状态栏第一行最前）。
  默认阈值 `disk_warn_mb=2048`、`disk_critical_mb=500`（`src/app/AppConfig.h`）。
- **可重跑**：同上命令（断言 `m8-03-02-*`）。

### E3 · M8-04#1（写满磁盘时空间耗尽前自动停止、成品完整无 0 字节）

- **判据**：提前停止 + 产物完整 + 不留 0 字节文件。
- **实测**：把停止线抬到「当前可用 - 2MB」触发同一条 `must_stop` 分支：
  `disk_stop=1`、`wall=2.08s`（请求 30s）、产物 2,110,041 字节、无 `.part` 残留、
  `mkv_probe.py` 判定 `RESULT: 0 failure(s)`。
- **可重跑**：同上命令（断言 `m8-04-01-*`）。
- **说明**：本条**不真的写满磁盘**——真写满会破坏机器且不可重复；触发的是完全相同的代码分支。

### E4 · M8-05#1（按码率与剩余空间估算可录时长，误差 ≤ 20%）

- **判据**：估算值与「按实际码率算出的可录时长」相对误差 ≤ 20%。
- **实测**：录 10s 得到实测码率 777,627 B/s；自由空间 22,643,912,704 B；
  估算 29,082s，按实测码率口径 28,947s，**相对误差 0.5%**。
- **口径说明**：估算公式是 `(free - stop) / (视频码率 + 192kbps 音频)`；
  若拿公式自己算自己等于恒等式、测不出偏差。本条用**实际写盘速率**做分母，
  才能量出「估算与现实差多少」。
- **可重跑**：同上命令（断言 `m8-05-01-estimate-error-20pct`）。

## 3. 回归与已知限制

- 本卡只改 `src/app/`（S5 文件域）与新增用例；`src/recorder/`、`src/core/` 未改。
- 已知限制：磁盘「剩余空间」是**目标卷**的可用量；输出目录跨卷时以输出目录所在卷为准
  （`StorageGuard::Refresh` 向上找已存在的父目录）。

## 5. 先红后绿对照（改前必 FAIL）

**对照方式**：仓库无版本控制（`git` 不存在），因此「改前」用**同工具链的临时回退构建**制造：
把本卡新增的两处代码用 `#if 0` 临时关闭（`RefreshDiskRemaining` 的水位判定 + `UpdateUi` 的提示拼接），
用**同一个** `build-msvc` 重建，跑同一条用例 → 断言必须 FAIL；随后立即恢复并重建 → 断言必须 PASS。

| 用例 | 改后（绿） | 改前（红，同一构建目录、临时回退） |
| --- | --- | --- |
| `verify/cases/g5_storage.ps1` | **13 PASS / 0 FAIL / 0 SKIP** | **9 PASS / 4 FAIL / 0 SKIP** |

红态明细（改前）：

```
  FAIL m8-03-01-ui-warn-logged — UI 日志记录了非阻塞提示
  FAIL m8-03-01-ui-warn-visible — 状态栏首行：就绪。输出目录: ... · 剩余可录 08:52:26 · 全屏
  FAIL m8-03-02-critical-logged — UI 日志记录了强提示
  FAIL m8-03-02-critical-visible — 状态栏首行：就绪。输出目录: ... · 剩余可录 08:52:38 · 全屏
```

即：**恰好是本卡新增的 4 条 UI 侧断言变红**，recorder 侧断言（M8-03#1 的非阻塞、
M8-04#1 的自动停止、M8-05#1 的估算误差）在两侧都绿——因为 recorder 侧本轮没改，
这正好说明断言测的是「本卡新增的 UI 呈现」，不是别的。

恢复后复跑：`RESULT: 13 PASS / 0 FAIL / 0 SKIP`（M8-05#1 误差 0.1%）。
