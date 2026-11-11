# EVIDENCE: S1-001 输出路径补卡

复现环境：本机 Win32 / MSVC 14.44.35207 Release / 双屏 1920×1080 / ffmpeg 已装
用例：`verify/cases/g3_output.ps1`（11 → 15 条断言）

---

## M5-15 同名不覆盖

- 验证方式：同一路径连续录两次，第二次必须换名；两份都要能解
- 命令：
  ```powershell
  pwsh -NoProfile -File verify/cases/g3_output.ps1 -BuildDir build-msvc
  ```
- 输出（A 组）：
  ```
  PASS first-keeps-name — output=...\build-msvc\output\same.mkv
  PASS second-renamed   — output=...\build-msvc\output\same_001.mkv
  PASS both-exist       — 412331 / 410804 bytes
  PASS first-playable   — container valid + decode clean
  PASS second-playable  — container valid + decode clean
  ```
- 结论：通过

## M5-15 / ER-07 录制期间目标被创建（D-24 回归断言）

- 验证方式：后台录 3s 到 `race.mkv`；1.5s（路径已准备、录制进行中）写入 32 字节哨兵并关闭；收尾后哨兵必须原样保留，成品换名
- 命令：`g3_output.ps1` D1 组
- 修复后输出：
  ```
  PASS target-created-late-kept — output=...\output\race_001.mkv sentinel_intact=True
  PASS race-recording-playable  — container valid + decode clean
  ```
  `race.mkv` 仍是 32 字节哨兵（目录实测 `race.mkv 32`）
- 结论：通过

### D-24 复现（修复前实测，非推演）

- 做法：临时回退 `CommitOutput` 的收尾避让（直接 `rename` 到 `plan.final_path`），重编后跑同一场景
- 输出：
  ```
  race.mkv length=1424401 sentinel_intact=False
  output=C:\Users\...\d24repro\race.mkv
  ```
- 含义：用户的 32 字节文件被成品整体覆盖，且进程仍报 `output=` 成功。与 ER-07「原文件未被破坏」直接冲突。
- 根因：MSVC `std::filesystem::rename` → `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)`
  （`...\MSVC\14.44.35207\crt\src\stl\filesystem.cpp:722`），目标存在时改名成功而非失败，因此「改名失败再避让」在目标已存在这一分支上永不触发
- 修复后同一场景：见上一条，`sentinel_intact=True`

## M8-07 目标被独占占用

- 命令：`g3_output.ps1` B 组（对 `same.mkv` 持 `FileShare.None` 句柄后录制）
- 输出：
  ```
  PASS recorded-despite-lock — record_exit=0 output=...\output\same_002.mkv
  PASS landed-on-new-name    — output=...\output\same_002.mkv
  PASS third-playable        — container valid + decode clean
  ```
- 结论：通过。备注：该场景由**准备阶段避让**满足（目标已存在），收尾重试分支仍未被触达；收尾分支的等效覆盖由 D1 提供（目标在准备之后出现）

## M8-06 路径不可建 / 不可写

- 命令：`g3_output.ps1` C 组（把输出指到一个普通文件下面）
- 输出：
  ```
  PASS preflight-rejected — record_exit=13
  PASS no-fragment        — exists=False
  PASS no-part-anywhere   — no .part left behind
  ```
- 结论：**前半通过；后半（录制中权限被收回 → 停止并保存到备用路径）未实现**，本轮不计为完成，挂账 T-16

## M5-04 `.part` 残留避让

- 验证方式：预置 `res.mkv.part`（16 字节垃圾）后录 `res.mkv`
- 输出：
  ```
  PASS stale-part-untouched — part_bytes=16（残留的 .part 不得被新录制覆盖）
  PASS residue-avoided-name — output=...\output\res.mkv
  ```
- 结论：通过。录制实际写 `res.mkv.1.part`，收尾改名到 `res.mkv`，旧 `.part` 原样保留（留给恢复流程）

---

## 契约与回归

| 检查 | 命令 | 结果 |
| --- | --- | --- |
| 契约 | `python verify/tools/contract_check.py` | 7 项 7 PASS / 0 FAIL |
| 存储守护 | `pwsh -File verify/cases/g3_storage.ps1` | 11 PASS / 0 FAIL |
| 崩溃恢复 | `pwsh -File verify/cases/g3_recovery.ps1` | 9 PASS / 0 FAIL（恢复 3.436s，丢失 2.56s，与 T-07 一致） |
| MSVC Release | `cmake --build build-msvc --config Release` | 零告警 |
| MinGW | `cmake --build build-mingw` | 零告警 |

产物目录残留核对（`build-msvc\output`）：除 D2 用例**人为预置**的 `res.mkv.part`（16B）外，无 `.part`。

## 未覆盖 / 已知限制

1. `_001~_999` 全占时的拒绝分支未做用例（需要预置 999 个文件，成本高于收益）；实现已改为「全占即返回失败」，代码路径可见 `FindFreePath`。
2. 收尾避让仍存在极小的 TOCTOU 窗口（查空位与 rename 之间），未采用「准备阶段占位」方案的理由：崩溃会留下 0 字节文件，与 G5-S3 冲突。
3. M8-06 后半未实现，见上。
