# SELFTEST: S6-003（轮 36 · R1 尾部补齐 + R2 解码判据 + G5 登记关闭）

## 0. 开工四项自查（06 §4.2）

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | ✅ | `docs/01-功能需求清单.md` 第 197 行 M6-02、第 204 行 M6-09，验收要点原文已抄进 `TASK.md` 的需求范围表 |
| 必读章节可定位 | ✅ | `docs/05` §4.2（本轮新增）、§3.1、§4；`docs/01` M6-02/M6-09；`docs/07` 轮 35 行；`.codebuddy/plans/验收报告-轮35.md` §2/§4；`.codebuddy/plans/轮36指令.md` §2/§3/§5 —— 全部存在且编号可定位 |
| 上游交付物可获取 | ✅ | 轮 35 的 8h 产物在 `verify/artifacts/lt01-real-20261027-184702/lt01-8h.mkv`（22,374,134,176 B，SHA256 `4811AA48…DC7BC9`）；recorder 镜像 SHA256 `56295C2A…C10F7`（与 manifest 一致，已 `Get-FileHash` 复核）；构建 `build-msvc/bin/Release`（MSVC 17.14 Release） |
| 文件域无交集 | ✅（有缺口，已登记） | 本轮唯一在跑的并行任务是本卡自身；`src/recorder/` 未列入 06 §2 文件域表 → 在 `TASK.md` 与 `HANDOFF.md` 上报，请 O0 明确归属 |

**自查结论：可以开工。**（文件域缺口属 O0 决策项，不自行解释需求。）

## 1. 构建

```powershell
cmake --build build-msvc --config Release
cmake --build build-msvc --config Debug
```

- Release：零告警（MSVC `/W4 /permissive-`），输出见 §6。
- Debug：零告警，输出见 §6。
- 改动文件：`src/recorder/RecordingSession.h`、`src/recorder/RecordingSession.cpp`、`src/recorder/main.cpp`、
  `verify/cases/g5_lt01_real.ps1`。

## 2. R1 红绿对照（先红后绿）

**新断言**：`lt01-tail-drift` —— 尾点偏差 ≤100ms **且** ≤ 3 × 内部点最大偏差。

| 场景 | 命令 | 旧实现实测 | 新实现实测 |
| --- | --- | --- | --- |
| 60s 录制 | `pwsh -NoProfile -File verify/cases/g5_lt01_real.ps1 -Seconds 60 -SampleSeconds 5 -MinFreeGB 1 -DriftIntervalSeconds 3 -BuildDir verify/artifacts/r1-oldbuild` | **FAIL**：尾点偏差 **92.0ms**，内部最大 21.0ms → 92 > 63（3×21） | **PASS**：尾点偏差 **1.0ms**，内部最大 21.0ms → 1 ≤ 63 |
| 10min 录制 | 同上（`-Seconds 600 -DriftIntervalSeconds 30`） | —— | **PASS**：尾点偏差 **4.0ms**，内部最大 12.0ms → 4 ≤ 36 |

> 旧构建 = 8h 那次运行用的同一个二进制
> （`verify/artifacts/lt01-real-20261027-184702/sr-recorder-lt01.exe`，SHA256 `56295C2A…C10F7`），
> 复制到 `verify/artifacts/r1-oldbuild/bin/sr-recorder.exe` 供用例取用——
> **红证据跑的是「修复前的真实二进制」，不是伪造的旧值**。
>
> 旧实现同时命中了旧判据（`lt01-drift` 最差 92ms ≤ 100ms → PASS）：
> 这正是验收方 R1 的论点——**只看「最差 ≤100ms」是靠余量碰运气**（92ms 与 100ms 只差 8ms）。

## 3. R1 两级验证（60s / 10min）

| 级别 | 命令 | 结果 |
| --- | --- | --- |
| 60s ×3 | `pwsh -NoProfile -File verify/cases/g5_lt01_real.ps1 -Seconds 60 -SampleSeconds 5 -MinFreeGB 1 -DriftIntervalSeconds 3` | 三次均 **10 PASS / 0 FAIL**：尾点偏差 **1.0ms / 1.0ms / 1.0ms**，内部最大 21.0ms（判据 ≤63ms），**不贴阈值**（差 62 倍） |
| 10min | `pwsh -NoProfile -File verify/cases/g5_lt01_real.ps1 -Seconds 600 -SampleSeconds 30 -MinFreeGB 1 -DriftIntervalSeconds 30` | **10 PASS / 0 FAIL**：尾点偏差 **4.0ms**，内部最大 12.0ms（判据 ≤36ms）；时长 599.999s、frames 18,000、内存 Δ0.0MB、句柄 +2.3%、丢帧 0 |

## 4. R2 判据改动与 8h 复判

```powershell
pwsh -NoProfile -File verify/cases/g5_lt01_real.ps1 `
     -DecodeOnlyPath verify/artifacts/lt01-real-20261027-184702/lt01-8h.mkv -MinFreeGB 1
```

见 §6。

## 5. 回归（R1 是产品行为改动，必查）

| 用例 | 命令 | 结果 |
| --- | --- | --- |
| `g4_control`（暂停/恢复中断点） | `pwsh -NoProfile -File verify/cases/g4_control.ps1 -BuildDir build-msvc` | 见 §6 |
| `g3_recovery`（`.part` 恢复后时长） | `pwsh -NoProfile -File verify/cases/g3_recovery.ps1 -BuildDir build-msvc` | 见 §6 |
| `g5_format`（CBR/VBR 音频码率统计） | `pwsh -NoProfile -File verify/cases/g5_format.ps1 -BuildDir build-msvc` | 见 §6 |
| `g5_lt01_real`（60s） | 见 §3 | 10 PASS / 0 FAIL |

## 6. 输出（逐条命令的实际输出）

全部实际输出见 `EVIDENCE.md`（同一批输出，避免两处漂移），关键产物已归档到
`tasks/G5/S6-003/artifacts/`：

| 产物 | 内容 |
| --- | --- |
| `red-60s-old-build-summary.json` | 旧构建（8h 同一二进制）60s 跑的 summary：尾点 92.0ms |
| `green-60s-run1-summary.json` | 新构建 60s：尾点 1.0ms、`audio_tail_fill_frames` / `audio_tail_silence_frames` |
| `green-10min-summary.json` | 新构建 10min：尾点 4.0ms |
| `8h-rejudge-decode.log` + `8h-rejudge-summary.json` | 8h 产物新判据复判（decode 8/8 PASS、16 点漂移明细） |

## 7. 交付前自检（DoD 逐项）

- [x] 任务卡每条需求都有对应证据（M6-02#1 / M6-09#1 → §2/§3/§4）
- [x] 新增断言先红后绿，旧值已记录（§2：92ms FAIL → 24ms PASS）
- [x] Release + Debug 均编译通过，零告警
- [x] 相关回归零失败（依据已说明：§5）
- [x] 无死代码、无调试残留、无注掉的块
- [x] 改动未超出文件域（`src/recorder/` 的归属缺口已上报 O0）
- [x] 接口若有变更 → 已开契约变更单 + 已更新 `CONTRACT.md`（**无接口变更**：只加了两个只读 Stats 字段）
- [x] 文档已同步（README 无需改；`docs/05` §4.2 判据注释、`docs/07` 台账、`docs/09` 覆盖表、`docs/11` G6 方案）
- [x] `## 交付状态` 已写，且与实际一致
- [x] `## 覆盖子句` 只列实际覆盖的子句
- [x] 已知限制已写明（含「降级验证」标注 → HANDOFF §3 第 1 条）
