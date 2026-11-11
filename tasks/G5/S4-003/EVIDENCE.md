# EVIDENCE: S4-003（M5-04 录制临时文件与停止时封装）

> 环境：本机 / Windows 11 24H2 / MSVC 17.14 Release / 虚拟显示
> 录制：6s 真实录制（`sr-recorder --record --seconds 6`），目标目录 = 用例自建的临时目录

---

## M5-04#1 —— 录制过程中目标目录存在临时文件且不可被误认为成品

**命令**：

```powershell
pwsh -NoProfile -File verify/cases/g5_tempfile.ps1 -BuildDir build-msvc
```

**输出（录制中途快照）**：

```text
--- 录制中：临时文件存在、成品不存在 ---
  PASS m5-04-part-exists — 录制中 .part 数量=1（期望 ≥1）：clip.mkv.part
  PASS m5-04-no-product-during-recording — 录制中 .mkv 成品数量=0（期望 0）
  PASS m5-04-part-named-from-product — 临时文件名=clip.mkv.part（期望 clip.mkv.part：与成品同名加 .part，停止时改名）
  PASS m5-04-list-not-product — --list 输出 kind=part=1 / kind=video=0（期望 ≥1 / 0：临时文件不得出现在成品条目里）
```

| 断言 | 落点（产品代码） | 说明 |
| --- | --- | --- |
| `m5-04-part-exists` | `src/recorder/RecordingSession.cpp`（录制中 `muxer.Open(part_path)`） | 录制中目录里确实有 `clip.mkv.part` |
| `m5-04-no-product-during-recording` | 同上 | 录制中**没有** `.mkv` 成品 → 临时文件不可能被当成成品 |
| `m5-04-part-named-from-product` | `src/recorder/OutputPath.cpp`（`PrepareOutputPath`） | 临时文件 = 成品名 + `.part`，停止时改名（不产生第二个随机名） |
| `m5-04-list-not-product` | `src/recorder/main.cpp`（`RunList` 打印 `entry=kind=part\|state=…`） | 产品列表（`sr-recorder --list`）里该条目是 `kind=part`，**没有** `kind=video` → 不会被当成成品 |

> 附加口径（既有证据，不在本用例内重复）：UI 的文件操作对 `.part` 走「临时文件」分支、
> 拒绝删除/改名，日志含「临时文件」——见 `verify/cases/g5_fileops.ps1:154-160`
> （`PASS part-protected — live.mkv.part 未被删除`）。

## M5-04#2 —— 停止后临时文件被清理，仅留一个成品

**输出（同一用例的后半段）**：

```text
--- 停止后：临时文件清理、只剩一个成品 ---
  PASS m5-04-record-exit — record_exit=0
  PASS m5-04-part-cleaned — 停止后 .part 数量=0（期望 0）：
  PASS m5-04-single-product — 停止后 .mkv 数量=1（期望 1）：clip.mkv
  PASS m5-04-product-duration — 成片时长 5.999s（期望 5~7s）
  PASS m5-04-product-decodable — 成片轨道：video,audio
  PASS m5-04-product-decode-clean — 解码 stderr 行数=0

RESULT: 10 PASS / 0 FAIL ; 产物 C:\Users\98468\AppData\Local\Temp\sr-tempfile
```

| 断言 | 落点（产品代码） | 说明 |
| --- | --- | --- |
| `m5-04-part-cleaned` / `m5-04-single-product` | `src/recorder/OutputPath.cpp`（`CommitOutput`：改名，失败则删临时文件） | 停止后 0 个 `.part` + 1 个 `.mkv` |
| `m5-04-product-duration` / `-decodable` / `-decode-clean` | `src/mux/MkvWriter.cpp` + `src/recorder/RecordingSession.cpp` | 成片 5.999s、video+audio 双轨、全片解码零错误 |

## 未覆盖的部分（诚实标注）

- **崩溃残留**：进程被强杀会留下 `.part`，由 `src/resilience/MkvRecovery.cpp` 离线修复
  （`g3_recovery.ps1` 9/9 覆盖），**不属于**「停止后清理」这条路径，故本用例不覆盖。
- **分片/合并**：`docs/01` M5-04 标题里的「临时分片 / 停止后合并」措辞与实现（单文件 + 改名）不一致，
  已作为文档缺陷项记录在 `tasks/G5/S4-003/HANDOFF.md`，**未改需求原文**。
