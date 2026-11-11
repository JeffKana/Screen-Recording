# SELFTEST: S4-003（M5-04 录制临时文件与停止时封装）

## 0. 开工四项自查（06 §4.2）

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | ✅ | `docs/01-功能需求清单.md` M5-04，验收要点原文已抄进 `TASK.md` 的需求范围表 |
| 必读章节可定位 | ✅ | `docs/02` §6（落盘与封装）、`docs/05` §5 AC-11（M5-04 + M8-01）、`CONTRACT.md` §3/§7 —— 均存在 |
| 上游交付物可获取 | ✅ | 轮 32/36 的 `src/recorder/OutputPath.cpp`、`src/mux/MkvWriter.cpp`；构建 `build-msvc/bin/Release` |
| 文件域无交集 | ✅ | 本卡只改 `verify/cases/g5_tempfile.ps1`（新增）+ 本卡文档；产品代码未改（能力已存在） |

## 1. 命令与输出

```powershell
pwsh -NoProfile -File verify/cases/g5_tempfile.ps1 -BuildDir build-msvc
```

```text
--- 录制中：临时文件存在、成品不存在 ---
  PASS m5-04-part-exists — 录制中 .part 数量=1（期望 ≥1）：clip.mkv.part
  PASS m5-04-no-product-during-recording — 录制中 .mkv 成品数量=0（期望 0）
  PASS m5-04-part-named-from-product — 临时文件名=clip.mkv.part（期望 clip.mkv.part：与成品同名加 .part，停止时改名）
  PASS m5-04-list-not-product — --list 输出 kind=part=1 / kind=video=0（期望 ≥1 / 0：临时文件不得出现在成品条目里）
--- 停止后：临时文件清理、只剩一个成品 ---
  PASS m5-04-record-exit — record_exit=0
  PASS m5-04-part-cleaned — 停止后 .part 数量=0（期望 0）：
  PASS m5-04-single-product — 停止后 .mkv 数量=1（期望 1）：clip.mkv
  PASS m5-04-product-duration — 成片时长 5.999s（期望 5~7s）
  PASS m5-04-product-decodable — 成片轨道：video,audio
  PASS m5-04-product-decode-clean — 解码 stderr 行数=0

RESULT: 10 PASS / 0 FAIL ; 产物 C:\Users\98468\AppData\Local\Temp\sr-tempfile
```

## 2. 回归

| 用例 | 结果 | 说明 |
| --- | --- | --- |
| `g5_tempfile`（新增） | **10 PASS / 0 FAIL** | 本卡 |
| `g3_recovery` | 9 PASS / 0 FAIL | `.part` 崩溃残留的离线修复路径未受影响 |
| `g5_fileops` | 9 PASS / 0 FAIL | `.part` 保护（UI 拒绝删除临时文件）未受影响 |
| `g5_format` | 14 PASS / 0 FAIL | 码率统计未受影响 |

## 3. 交付前自检

- [x] 两条子句各有独立断言（一个需求一条证据）
- [x] 每条断言都有可重跑的命令
- [x] 本轮无产品代码改动 → 无「先红后绿」需求（新用例是覆盖补齐，不是行为修复；
      用例在**已存在**的能力上直接绿，属「回归护栏」而非修复证据，已在 EVIDENCE 标注）
- [x] Release + Debug 零告警
- [x] 文件域无越界（本卡只动 verify/cases 新增 + tasks/ 自身）
- [x] `vcpkg.json` 未改
- [x] HANDOFF 四问齐答
- [x] `## 交付状态` = 已实现（与实际一致）
- [x] `## 覆盖子句` 只列 M5-04#1/#2
- [x] 已知限制已写明（崩溃残留路径不在本用例范围；分片措辞差异）
