# HANDOFF: S4-002（M3-05 编码器列表）

## 1. 我改了什么接口？

**没有改公开接口**（`CONTRACT.md` §4 的 `IVideoEncoder` / `VideoEncoderConfig` 一行未动，
`src/core/` 一行未动）。新增的都是**只读观测**与**新增能力**：

| 位置 | 新增 | 用途 |
| --- | --- | --- |
| `src/encode/EncoderList.h` | `EncoderInfo` / `ListEncoders()` / `FormatEncoderList()` / `FindEncoder()` | 编码器列表与规范单行序列化（新文件，S4 域） |
| `src/encode/MfVideoEncoder.h` | `SetRequestedId()` / `RequestedId()` | 显式指定编码器（**具体类**上的方法，不进核心接口） |
| `src/recorder/RecordingSession.h` | `Options::encoder_id` | 录制选项（空 = 自动选择） |
| `sr-recorder` CLI | `--list-encoders` / `--encoder <id>` | 列表查询 / 强制指定 |
| `sr-ui` CLI | `--show-settings`（`SR_TEST_HOOKS` 内） | 测试钩子：启动即打开设置页 |

**产品行为变更**：显式指定编码器时**不做「硬编 → 软编」回退**（点名了就不许悄悄换）。

## 2. 下游需要接什么？

| 下游 | 要做什么 |
| --- | --- |
| 验收方 | 复跑 `pwsh -NoProfile -File verify/cases/g5_encoder_list.ps1 -BuildDir build-msvc`（期望 17 PASS / 0 FAIL） |
| 验收方 | 覆盖表终态：**87 已实现 / 4 有卡未实现 / 25 无卡**（M3-05#1/#2 由「有卡未实现」转「已实现」） |
| S5 | 若后续要让用户在设置页**选择**编码器（不只是显示），需要把列表项做成可选控件并把 id 传回 recorder —— 本轮只做「显示」 |
| G6 | `--list-encoders` 是产品命令，**不是**测试开关 → 交付包里保留 ✓；`--show-settings` 是测试钩子，`SR_TEST_HOOKS=OFF` 的发布包不含 ✓ |

## 3. 已知限制是什么？

1. **`available` 的口径是「MFT 能被激活」**，不是「一定能用于这次录制」——协商/设备仍可能失败
   （产品按 M3-06 回退软编）。UI 上的文案没有区分这两种「可用」，如验收方认为需要更精确，
   可在 `EncoderInfo` 上再加一个 `negotiable` 字段（本轮未做）。
2. **MF 的友好名取不到**（本机 `MFT_FRIENDLY_NAME` 为空），显示名退回固定文案；
   判定与选择都用稳定 `id` ✓。
3. **厂商项永远不可用**（未集成厂商 SDK、不引三方依赖）——它们的作用是告诉用户「为什么没有硬编」。
4. **探测不进录制启动路径**（只在设置页首次打开 / `--list-encoders` 时做），
   但**每次打开设置页都会重跑一次**探测（当前没有缓存；探测耗时 <100ms）。
5. **`--encoder` 只在 CLI 上有入口，UI 暂不能选**（见上）。

## 4. 哪些需求我**没**做完？

| 需求 | 状态 | 没做完的部分 |
| --- | --- | --- |
| M3-05#1 | **已实现** | 无（列表 ≥1 项、含 `mf-h264-sw`、UI 展示，三条都有独立断言） |
| M3-05#2 | **已实现** | 无（每条不可用项都有真实探测原因） |
| M3-05 的「可选择」延伸（UI 里选一个编码器并传给 recorder） | 未做 | 本轮只做「显示」；需求原文只要求「显示实际可用列表」✓ |
| `x264` 项 | 不做 | 本产品不引三方依赖；需求原文是「如 …」非穷举 |

> **四件套状态**：本卡（TASK/EVIDENCE/SELFTEST/HANDOFF）已补齐 —— 轮 36 建卡阶段只有 TASK+HANDOFF，
> 按验收方轮 36 报告 §3 的附注，本轮补上 EVIDENCE/SELFTEST。
