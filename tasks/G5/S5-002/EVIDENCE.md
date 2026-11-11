# EVIDENCE: G5/S5-002（M5-06 录制文件列表）

> 口径声明: 列表数据全部来自 `sr-recorder --list`；「可恢复」与 `--scan` 共用
> `MkvRecovery::Probe` 的 `repairable`，不存在第二套判定。
> 数据来源: 2026-10-26 11:52 / `build-msvc` Release / 本机

## M5-06#1 列表按时间倒序

- 验证方式: 造 5 个条目（3 个成品 + 1 可恢复 .part + 1 不可恢复 .part），
  启动 `sr-ui --show-list`，比对列表顺序与文件系统 mtime 倒序
- 命令: `pwsh -NoProfile -File verify/cases/g5_list.ps1 -BuildDir build-msvc -Config Release`
- 输出: 控制台 PASS 行（本轮 24/24）；日志 `verify/artifacts` 外仍可复跑
- 结论: **通过**

| 断言 | 实测 |
| --- | --- |
| `list-desc-order` | `broken.mkv.part, crash.mkv.part, c.mkv, b.mkv, a.mkv`（与 mtime 倒序一致） |
| `list-mtime-descending` | 11:52:24.686 > 24.678 > 24.511 > 21.954 > 17.434 |
| `list-count` | 5 |

命令（直接看原始输出）: `build-msvc\bin\Release\sr-recorder.exe --list <dir>`

```
count=5
entry=kind=part    state=broken      size=21      duration_ms=0    w=0    h=0    name=broken.mkv.part
entry=kind=part    state=recoverable size=839228  duration_ms=999  w=1920 h=1080 name=crash.mkv.part
entry=kind=video   state=complete    size=773692  duration_ms=999  w=1920 h=1080 name=c.mkv
entry=kind=video   state=complete    size=2323539 duration_ms=2999 w=1920 h=1080 name=b.mkv
entry=kind=video   state=complete    size=1607200 duration_ms=1999 w=1920 h=1080 name=a.mkv
```

## M5-06#2 录制中的条目实时更新时长

- 验证方式: UI 录制中打开列表，1s 定时器刷新「录制中」行；日志留 `list_live`
- 命令: 同上用例 D 段（热键 F9 起录 → 打开列表 → 采样 3.5s）
- 输出: `list_live name=live.mkv.part duration_ms=34 / 1049 / 2050 / 3082`
- 结论: **通过**

| 断言 | 实测 |
| --- | --- |
| `list-recording-row` | 1 行 `state=recording` |
| `list-live-updates` | 4 次采样，首 34ms → 末 3,082ms，增长 3,048ms |
| `list-after-stop` / `-duration` | 停止后出现 `live.mkv` 且 `duration_ms=3599` |

## M5-06#3 异常中断的条目标注「可恢复」

- 验证方式: 造两类 `.part`（可截断修复 / 完全不是 MKV），分别跑 `--scan` 与列表，比对集合
- 命令: `sr-recorder --scan <dir>`；`sr-recorder --list <dir>`
- 输出: `file=crash.mkv.part repairable=1`；`file=broken.mkv.part repairable=0`
- 结论: **通过**

| 断言 | 实测 |
| --- | --- |
| `scan-baseline` | scan repairable=[crash.mkv.part] broken=[broken.mkv.part] |
| `list-recoverable-matches-scan` | list=[crash.mkv.part] scan=[crash.mkv.part] |
| `list-broken-matches-scan` | list=[broken.mkv.part] scan=[broken.mkv.part] |
| `list-recoverable-badge` | `crash.mkv.part state=recoverable duration_ms=999`（修复后前 999ms 可救） |

UI 上的状态列文案：`已完成` / `录制中` / `可恢复` / `不可恢复`。

## 复现环境

本机 / Windows 11 24H2 / MSVC 14.44 Release / 分辨率 1920×1080
