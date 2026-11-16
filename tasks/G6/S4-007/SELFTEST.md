# SELFTEST · S4-007（轮 49 · M3-09#1/#2/#3 收口）

## §0 开工四项自查（来自 `docs/06 §4.2`，缺一不开工）

| 检查项 | 通过标准 | 本轮实测 | 结论 |
| --- | --- | --- | --- |
| 需求编号可检索 | 需求号能在 `docs/01` 查到原文 | `M3-09` 在 `docs/01:111`；验收要点原文「MP4 优先 faststart 布局；MKV 支持多音轨与更长容错；切换格式不影响录制流程」 | ✅ |
| 必读章节可定位 | 卡里列的章节在文档中存在 | `docs/05 §7.2`（:214）、`CONTRACT.md §18.2`（:279 进程退出码表）、`docs/22 §1/§2`、`docs/02 §7`（ADR-02） | ✅ |
| 上游交付物可获取 | 上游版本/分支/接口可拿到 | `main` @ `ed8767a`（PR #9 + PR #10 已合并）；`build-msvc` 可直接编译（VS17 x64） | ✅ |
| 文件域无交集 | 与当前并行任务不重叠 | `S4-007` = `src/mux`、`src/recorder`、`src/resilience`、`src/app`、三个用例；`V1-022` = `docs/*`、`tasks/*`。无交集 | ✅ |

---

## §1 构建（Release + Debug 零告警）

```powershell
cmake --build build-msvc --config Release
cmake --build build-msvc --config Debug
cmake --build build-mingw
```

```
sr-recorder.vcxproj -> ...\build-msvc\bin\Release\sr-recorder.exe
sr-ui.vcxproj       -> ...\build-msvc\bin\Release\sr-ui.exe
srctl.vcxproj / srprobe.vcxproj 均生成
debug_exit=0                      # 无 error / 无警告行
mingw_exit=0                      # 无 error / 无警告行
```

> 告警拦截口径与 CI 一致：`Select-String -Pattern 'warning C\d+|warning:'` 零命中。

---

## §2 用例（可重跑命令 + 实际 RESULT 行）

```powershell
pwsh -File verify/cases/g5_mp4.ps1
pwsh -File verify/cases/g5_multiaudio.ps1
pwsh -File verify/cases/g5_ui_format.ps1
python verify/tools/mp4_box_check.py "$env:TEMP\sr-mp4\r48.mp4"
```

```
RESULT: 19 PASS / 0 FAIL ; 产物 C:\Users\98468\AppData\Local\Temp\sr-mp4
RESULT: 10 PASS / 0 FAIL ; 产物 C:\Users\98468\AppData\Local\Temp\sr-multiaudio
RESULT: 13 PASS / 0 FAIL ; 产物 C:\Users\98468\AppData\Local\Temp\sr-ui-format
RESULT: 43 PASS / 0 FAIL ; file=r48.mp4
```

`g5_mp4` 的 19 条里，原有 13 条全部保留且 PASS（`r48-*`），新增 6 条：

```
PASS r49-faststart-moov-before-mdat
PASS r49-faststart-moov-in-first-64k
PASS r49-part-written-without-moov
PASS r49-part-unrepairable-with-reason
PASS r49-part-kept-intact
PASS r49-part-no-final-output
```

---

## §3 回归（文件域抽样 + 受影响链路全量）

```powershell
pwsh -File verify/cases/g3_playback.ps1      # 11 PASS / 0 FAIL
pwsh -File verify/cases/g3_recovery.ps1      #  9 PASS / 0 FAIL
pwsh -File verify/cases/g4_gate.ps1          # 12 PASS / 0 FAIL
pwsh -File verify/cases/ui_smoke.ps1  ×3     # 103 PASS / 0 FAIL / 0 SKIP ×3
```

```
RESULT: 11 PASS / 0 FAIL        (g3_playback：容器/编码/分辨率/帧率/时长/全解/帧数/三帧非空)
RESULT: 9 PASS / 0 FAIL         (g3_recovery：强杀 .part → 可修复 → 修完可解、丢帧 ≤10s)
RESULT: 12 PASS / 0 FAIL        (g4_gate：60s 端到端，pid 无残留，帧数 1807/1806)
RESULT: 103 PASS / 0 FAIL / 0 SKIP   ×3    (ui_smoke：热键/录制/托盘/列表/区域/自检)
```

依据：`src/mux` 两个封装器与 `src/recorder`、`src/app` 都被本轮改动，因此**跑全量相关链路**
（MKV 播放/恢复、G4 端到端、UI 冒烟）；未改编码/采集/存储，故未跑 `g5_format` / `g5_storage`
（它们在轮 48 刚做过 5 连复核，本轮 `src/encode`、`src/recorder/StorageGuard` 零改动）。

---

## §4 红端（先红后绿）

```powershell
git worktree add "$env:TEMP\sr-old-tree" ed8767a
cmake -S "$env:TEMP\sr-old-tree" -B "$env:TEMP\sr-old-tree\build" -G "Visual Studio 17 2022" -A x64
cmake --build "$env:TEMP\sr-old-tree\build" --config Release
$old = "$env:TEMP\sr-old-tree\build\bin\Release"
pwsh -File verify/cases/g5_mp4.ps1 -RecorderPath "$old\sr-recorder.exe"
pwsh -File verify/cases/g5_multiaudio.ps1 -RecorderPath "$old\sr-recorder.exe"
```

```
RESULT: 15 PASS / 4 FAIL        (g5_mp4 红端：两条 faststart + box 口径 + .part 原因)
RESULT: 0 PASS / 10 FAIL        (g5_multiaudio 红端：无 --audio-tracks)
```

`g5_ui_format` 红端不可自动执行（旧 UI 忽略 `--set-container` 后常驻，脚本会挂住），红证据改用
源码事实 `ed8767a:src/app/main.cpp:376` = `directory / (name + L".mkv")`。详见 `EVIDENCE.md §5`。

---

## §5 静态检查

```powershell
python verify/tools/coverage_report.py --check
python verify/tools/contract_check.py
python verify/tools/card_consistency_check.py
python verify/tools/domain_table_check.py
```

```
coverage table is up to date
需求库 144 条, V1.0 66 条 / 116 子句, 已实现 111, 有卡未实现 1, 无卡 4
汇总: 8 项检查, 8 PASS, 0 FAIL
card_consistency: OK —— 扫描 64 张卡，0 处冲突
MISMATCH_COUNT=0
```

---

## §6 交付前逐项确认

- [x] 任务卡每条需求都有对应证据（`EVIDENCE.md §1~§4`，一需求一条）
- [x] 新增断言都先红后绿，旧值已记录（`§4` + `EVIDENCE.md §5` 对照表）
- [x] Release + Debug 均编译通过，零告警（`§1`，另含 MinGW Release）
- [x] 相关回归零失败（`§3`，已说明依据：受影响链路全量 + 文件域抽样）
- [x] 无死代码、无调试残留、无注掉的块
- [x] 改动未超出任务卡声明的文件域（`git status --short` 逐项对照白名单）
- [x] 接口若有变更 → 未改 core 接口/IPC；recorder CLI 新增 `--audio-tracks` 已写 usage，退出码口径沿用 `CONTRACT §18.2` 的「2 = 参数错误」
- [x] 文档已同步（`docs/01` / `docs/07` / `docs/09` / `docs/22` 由 `V1-022` 回写；本卡 `## 交付状态` 与实际一致）
- [x] `## 覆盖子句` 只列实际覆盖的子句（`M3-09#*` 三条）
- [x] 已知限制已写明（`HANDOFF.md §4`：MP4 恢复 V1.1、双音轨同源镜像、faststart 收尾一次全量搬移）
