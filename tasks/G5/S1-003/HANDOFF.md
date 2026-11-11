# HANDOFF: LT-01/LT-02 长录制代理

## 怎么重跑

```powershell
# LT-01 代理（默认 3 轮 × 4 段 × 1800s ≈ 6h）
pwsh -NoProfile -File verify/cases/g5_lt01.ps1 -Rounds 3 -Segments 4 -Seconds 1800

# 冒烟（2 分钟，验证链路而不是稳定性）
pwsh -NoProfile -File verify/cases/g5_lt01.ps1 -Rounds 1 -Segments 4 -Seconds 20

# LT-02 代理（30min 静音 + 30s 恢复 ≈ 32min）
pwsh -NoProfile -File verify/cases/g5_lt02.ps1 -MuteSeconds 1800 -RecoverySeconds 30
```

产物在 `verify/artifacts/lt01-<时间戳>/`、`verify/artifacts/lt02-<时间戳>/`，
含 `summary.json` 与每段 `.mkv`/`.log`/`.out`。

## 实现约束

1. **不能挂 UI**：UI 每秒快照/托盘刷新会改变内存基线，结论不可比。
2. **不能并发**：`sr-recorder` 有单实例锁（`%TEMP%\sr-recorder.lock`），第二个实例 exit=4。
   所以 LT-01 与 LT-02、以及任何其它录像用例都必须串行。
3. **段间必须退出进程**：脚本每段 `Start-Process` → `WaitForExit` → 下一段。
4. 内存判定用 `mem_peak_mb`（PeakWorkingSet64），句柄用 `handles`；两者都在 recorder 汇总里。

## 当前结论与剩余

- 代理链路已通（4×20s 冒烟：mem 543.4→544.1MB +0.1%，handles 387→387，时长 20.0s）。
- **未完成**：12 段 × 30min 全量受会话时长限制，实际完成段数见 `docs/10` 第 4 节。
- 若要收口 M6-02#1/M6-09#1，需要一次**8 小时单文件**运行（新卡，轮 32/33 排期）。

## 已知风险

- 30fps/1080p 每段约 900MB~1.2GB，12 段约 12GB；`summary.json` 里记录了 `file` 路径，
  跑完请清理 `verify/artifacts/lt01-*` 的 `.mkv`（可用 `-KeepVideos:$false` 自行处理）。
- 静音段的 `mean_volume` 依赖「期间真的没有声音」；若系统通知音恰好触发，会误判失败。
