# SELFTEST: S4-002（M3-05 编码器列表）

## 0. 开工四项自查（06 §4.2）

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | ✅ | `docs/01` M3-05，验收要点原文（「界面显示实际可用列表…」「不可用项不出现或标注不可用原因」）已抄进 `TASK.md` |
| 必读章节可定位 | ✅ | `docs/01` M3-01~M3-11、`docs/02` §7、`docs/05` §5 AC-08、`CONTRACT.md` §4 —— 均存在 |
| 上游交付物可获取 | ✅ | `src/encode/MfVideoEncoder.cpp`（轮 3/8/32 的产物）、`src/recorder/main.cpp`、`src/app/main.cpp`；构建 `build-msvc/bin/Release` |
| 文件域无交集 | ✅ | 本轮只动 `src/encode/`（S4）、`src/recorder/`（S1，轮 37 裁定归属）、`src/app/`（S5）、`verify/cases/g5_encoder_list.ps1`（新增用例）；与并行的 `tasks/G5/V1-002`（只动 verify/tools + docs）不重叠 |

## 1. 构建

```powershell
cmake --build build-msvc --config Release   # 0 error / 0 warning
cmake --build build-msvc --config Debug     # 0 error / 0 warning
cmake --build build-mingw --config Release   # 0 error / 0 warning
```

## 2. 新用例

```powershell
pwsh -NoProfile -File verify/cases/g5_encoder_list.ps1 -BuildDir build-msvc
```

```text
RESULT: 17 PASS / 0 FAIL
```

## 3. 回归

| 用例 | 结果 | 说明 |
| --- | --- | --- |
| `g5_encoder_list`（新增） | **17 PASS / 0 FAIL** | 本卡 |
| `g5_format` | 14 PASS / 0 FAIL | 码率/编码路径未受影响 |
| `g4_gate` | 12 PASS / 0 FAIL | 集成门未受影响 |
| `g5_fileops` | 9 PASS / 0 FAIL | 文件操作未受影响 |
| `g3_recovery` | 9 PASS / 0 FAIL | 恢复路径未受影响 |
| `ui_smoke` ×3 | 103 PASS / 0 FAIL / 0 SKIP ×3 | 设置页新增控件不影响主流程；UI 日志多了一行 `encoders=[...]` |
| 契约 `contract_check.py` | 8/8 PASS | C8 缺理由 0 |

## 4. 交付前自检

- [x] 开工四项自查全部通过并已记录
- [x] 两条子句各有独立断言（一个需求一条证据）
- [x] 每条断言都有可重跑的命令与实际输出
- [x] 新增断言先红后绿：**产品行为部分是新增能力**（此前全仓无编码器列表结构），
      红的证据是轮 36 的代码普查结论（`grep` 零命中，见 `TASK.md` 的现状表）；
      「强行指定不可用编码器 → exit 6」在实现前**无法成立**（CLI 无 `--encoder` 参数），
      实现后立刻成立 ✓
- [x] Release + Debug + MinGW 零告警
- [x] 相关回归零失败（依据已说明：§3）
- [x] 无死代码、无调试残留、无注掉的块
- [x] 改动未超出文件域（`src/encode/`、`src/recorder/`、`src/app/`、`verify/cases/` 新增；
      **`src/core/` 一行未动** —— 编码器 id 通过 `MfVideoEncoder::SetRequestedId` 传，
      不改 `IVideoEncoder` 契约 ✓）
- [x] 接口未变 → 无需契约变更单（`CONTRACT.md` 未动）
- [x] 文档已同步（`TASK.md` 交付状态、`docs/09` 覆盖表、`docs/07` 台账）
- [x] HANDOFF 四问齐答
- [x] `## 交付状态` = 已实现（与实际一致）
- [x] `## 覆盖子句` 只列 M3-05#1/#2
- [x] 已知限制已写明（含 `available` 的口径与 MF 友好名取不到）
