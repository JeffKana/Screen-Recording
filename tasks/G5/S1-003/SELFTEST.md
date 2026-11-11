# SELFTEST: LT-01/LT-02 代理

## 1. 链路自检（2 分钟，先跑这个）

```powershell
pwsh -NoProfile -File verify/cases/g5_lt01.ps1 -Rounds 1 -Segments 4 -Seconds 20
```

期望：4 段 `exit=0 media=20.0 playable=True`，轮汇总 `mem +≤5%`、`handles +≤10%`，退出码 0。
这条只验证「指标采集 + 判据 + 取证」链路，**不能**当稳定性结论。

## 2. 全量代理

```powershell
pwsh -NoProfile -File verify/cases/g5_lt01.ps1 -Rounds 3 -Segments 4 -Seconds 1800
```

- 每段：`exit=0`、`media=1800±1s`、`playable=True`；
- 每轮：`mem_peak` 增幅 ≤5%、`handles` 增幅 ≤10%；
- 产物：`verify/artifacts/lt01-*/summary.json`（`failures=0`）。

## 3. 音频静音/恢复

```powershell
pwsh -NoProfile -File verify/cases/g5_lt02.ps1 -MuteSeconds 1800 -RecoverySeconds 30
```

期望：`mute-mean-50db` PASS（实测 -91dB）、`recovery-audio-present`、
`recovery-no-clip`（max ≤ -1dB）、`recovery-onset-found`、`recovery-no-pop`。

先跑 20s 版本确认环境（`-MuteSeconds 15 -RecoverySeconds 20`），再上 30 分钟。

## 4. 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 第二段 `exit=4` | 上一段没退干净（单实例锁） | 检查 `Get-Process sr-recorder`，确认段间 `WaitForExit` |
| `recovery-audio-present` FAIL | 系统音量为 0 / 输出设备静音 | 调音量后重跑；该失败不是产品问题 |
| `mute-mean-50db` FAIL | 录制期间有系统提示音/其它应用出声 | 静音环境重跑 |
| `playable=False` | 编码器/容器异常 | 看同目录 `.log` 与 `verify/artifacts/fail-*` |
