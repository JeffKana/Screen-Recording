# SELFTEST: M3-03 / M3-04 引擎级

```powershell
pwsh -NoProfile -File verify/cases/g5_format.ps1 -Seconds 10
```

预期（当前基线）：

| 检查 | 结果 |
| --- | --- |
| `m3-03-60fps-rate` | PASS，`encode_fps≈59` |
| `m3-03-60fps-frames` | PASS，`frames≈60×时长` |
| `m3-03-15fps-cap` | PASS，`encode_fps≤15.5` |
| `m3-03-15fps-frames` | PASS，帧数 ≤ 15×wall+2 |
| `m3-04-cbr-spread` | **FAIL（已知缺陷）**，spread≈160% |
| `m3-04-cbr-nominal` | **FAIL（已知缺陷）**，avg≈2.5Mbps vs 6Mbps |

脚本整体退出码 1 是**预期**的：它把 M3-04 的缺口显性化，不是脚本坏了。
接入 CBR 后应转为全绿，再去做 UI 入口。
