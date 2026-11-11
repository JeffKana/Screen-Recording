# HANDOFF: S6-002（轮 33 / 轮 34）

## 1. 我改了什么接口？

- 新增用例 `verify/cases/g5_lt01_real.ps1`（参数：`-Seconds` / `-SampleSeconds` /
  `-MinFreeGB` / `-SkipFullDecode`）。
- **未改产品接口**：8h 运行只用既有 `sr-recorder --record --fps 30 --bitrate 6000 --rate-mode cbr`。
- 轮 34：长跑把 `sr-recorder.exe` 复制为 `<work>/sr-recorder-lt01.exe` 再启动，
  镜像名与普通用例的 `sr-recorder` 区分（D-60）；启动写
  `verify/artifacts/.lt01-running.json`，收尾/trap 删除。
- 产物约定：`verify/artifacts/lt01-real-<yyyyMMdd-HHmmss>/`
  （`lt01-8h.mkv`、`samples.jsonl`、`lt01-8h.log`、`lt01-8h.out`、`summary.json`）。

## 2. 下游需要接什么？

- 8h 结束后先读 `summary.json`：`aborted`、`record_exit`、`span_s`、`probe_path`、
  `recorder_sha256`、`duration_s`、`mem_baseline_mb`、`mem_peak_mb`（PeakWorkingSet64）、
  `ws_peak_mb`、`mem_hourly_mb`、`handles_*`、`dropped`、`drift_points`、`worst_drift_ms`。
- `aborted=true` 表示进程被外部终止（或未写出 `record_exit`），此时不要看
  `duration_s`/`drift_points`，直接按 DEFECT-01 的流程定位分钟。
- 只有 `fail=0` 才把 `tasks/G5/S6-002/TASK.md` 的 M6-02 / M6-09 改成「已实现」，
  再 `python verify/tools/coverage_report.py --write`；否则把失败项写进 `docs/07` 并保持挂账。
- 验收方复跑时**不要**同时跑任何构建或用例；`-SkipFullDecode` 只用于调试，
  正式判定必须全解。

## 3. 已知限制是什么？

1. **外部采样 60s 粒度**：可能漏掉短时尖峰；所以同时保留 recorder 自报
   `mem_peak_mb`（PeakWorkingSet）做交叉核对，两者不一致时以较大者为准并记录。
2. **内存基线取 9~11min 均值**：如果前 10 分钟有一次性分配（编码器/采集初始化），
   基线会偏高，判据会偏宽松；这是按 05 §3「第 8 小时均值 − 第 10 分钟均值」的口径实现。
3. **漂移只测文件内 PTS**：不测「拍手 + 秒表」的物理音画同步（LT-02），
   不能替代 M6-09 的人工项。
4. **机器必须保持唤醒**：第 3 轮已起 `SetThreadExecutionState` 守护进程（pid 44344，10h 后退出）；
   如果该进程被杀且系统进入睡眠，丢帧/时长会失真，本次作废。
5. **屏幕内容会被录制**：8h 期间不要打开敏感窗口；本轮的运行目录与日志会包含屏幕内容。
6. **并发用例**：`.lt01-running.json` 存在时不要跑任何用例。长跑镜像名已是
   `sr-recorder-lt01`，按 `sr-recorder` 名字清理的用例不会误杀；但 CPU/磁盘争用仍会污染基线。

## 4. 哪些需求我**没**做完？

- **M6-02#1**：8h 内存判据已达标（Δ1.3MB、无爬升），但按「六项全 PASS 才转」保持
  「有卡未实现」。
- **M6-09#1**：全片解码出现 6,667 条 null-muxer DTS 警告（解码零错误、864,000 帧）；
  待验收方裁定口径（DEFECT-02）前不转「已实现」。
- **LT-02（8h 音频漂移，拍手/秒表）**：不在本卡，未做。
- **LT-01 连续 3 次**：轮 33/34/35 共启动 3 次，1 次成功收尾（本轮）；
  连续 3 次有效仍为 0，V1.0 发布判据未满足。
