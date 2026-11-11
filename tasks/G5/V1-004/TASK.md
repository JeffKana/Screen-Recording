# TASK: V1-004 轮 41 · 无卡收口 5 条（补卡 + 专属行为断言）

阶段：G5｜角色：V1（验收侧）｜轮次：41｜来源：验收方 `.codebuddy/plans/轮41指令.md` §1 P0 / §3 / §4.3

> 本卡只做**清账**：挑 5 条「产品能力已在、只缺 TASK 追溯与专属断言」的无卡子句。
> 不新增采集/编码能力，不改合并规则，不改 `docs/01` 判据。
> 降级/代理验证逐条显式标注；口径冲突单独提裁定申请，不静默解释。
> **不自评为已验收**，结论待验收方独立重跑。

## 需求范围（验收要点原文，判据以 `docs/01` 为准）

| 子句 | 验收要点原文（`docs/01`） | 证据形式 |
| --- | --- | --- |
| M8-01#1 | 录制 5 分钟后强制结束进程，重启后可恢复到崩溃前 ≤ 10s 的内容，文件可正常播放 | 强杀→`--scan`→`--repair`→ffprobe 时长 / ffmpeg 解码（**降级验证：12s 短录代理 5min 长录**） |
| M6-10#1 | 安装包无驱动组件 | zip 条目清单 + `install.ps1` + `src/` 源码三方静态断言 |
| M6-11#1 | 面板数值随录制状态更新 | recorder 1Hz `metrics` 日志 + UI 状态栏就绪/录制两态对照 |
| M3-15#1 | 人为制造 GPU 压力导致实际 45fps（配置 60fps），成片时长仍准确、音画同步、无加速播放现象 | `--inject-drop-every 4` 注入 + 成片 duration / avg_frame_rate / 音轨（**代理验证：注入 ≠ 真实 GPU 压力**） |
| M6-04#2 | 均失败时落到 x264 且录制成功 | `--list-encoders` 三厂商不可用原因 + 软编回退录制成功 + 全失败双分支（**按 `docs/03` 冻结选型的软编等价口径；术语冲突见 `docs/15` 裁定记录（轮 42 已裁定）**） |

## 必读章节

- `docs/01-功能需求清单.md`：M8-01、M6-10、M6-11、M3-15、M6-04（**验收要点原文**）
- `docs/03-技术选型.md` §编码选型（x264 / MF 软编的 GPL 决策）
- `docs/05-非功能需求与验收.md` §1 参考机型、§2.1 测量方法、§7.2 准入门槛
- `docs/06-子智能体执行规程.md` §4.2 开工四项自查、§4.3 降级验证标注
- `docs/12-轮34-VBR测法裁定记录.md`（同类文档冲突先例）
- `verify/lib/Cleanup.ps1`（`Read-TextSafe` 的共享读语义）

## 文件域（白名单）

- `verify/cases/g5_closeout.ps1`（新增，5 条各 ≥1 条行为断言）
- `docs/09-V1.0覆盖表.md`（生成物）、`docs/15-轮42-M6-04-2-x264口径裁定记录.md`（新增）
- `tasks/G5/V1-004/*`（本卡四件套）
- `tasks/G6/V1-002/artifacts/g5_closeout.txt`（证据）

> 不改 `src/`、不改 `CONTRACT.md`、不改 `coverage_model.py` / `coverage_report.py`（合并规则）。
> `verify/cases/g5_storage.ps1` / `g5_estimate_selftest.ps1` / `verify/tools/build_release.ps1`
> 由 `tasks/G6/V1-002` 独占；`src/recorder/main.cpp` 由 `tasks/G5/S1-009` 独占；三卡目录互斥、无交集。

## 上游交付

- 轮 40 覆盖表 94/1/21；`docs/09` 里 M8-01#1 的挂账理由已写明「已实现并经 … 验证；缺 TASK.md 追溯，待补卡」
- 既有实现：`src/resilience/MkvRecovery.cpp`、`packaging/install.ps1`、`src/app/main.cpp` 的 `FormatMetrics`、
  `src/recorder/RecordingSession.cpp` 的 `inject_drop_every`、`src/encode/EncoderList.cpp`
- 既有用例：`g3_recovery.ps1` 9/9、`g5_inject.ps1` 28/28、`g5_encoder_list.ps1` 17/17、`g5_fallback.ps1` 25/25

## 约束

1. **不自行解释判据**：M6-04#2 的 01 原文点名 x264，03 选型已排除；只按「软编等价」给行为证据，
   并提 `docs/15` 裁定记录（轮 42 已裁定），正文不写成「x264 已实现」。若验收方裁定 B，下一轮回退为「有卡未实现」。
2. **降级/代理验证必须显式标注**：M8-01#1 的 12s 代理 5min、M3-15#1 的注入代理 GPU 压力，
   断言文案与 EVIDENCE 都要写；不得写成全量实测。
3. 取数失败一律 SKIP，不得用 0 算结论（与 R1 三档语义一致）。
4. 一需求一条证据，每条含可重跑命令与实测数值。
5. C8：剩余未实现子句逐条挂账理由不变；`--check` 必须通过。

## 完成定义（DoD）

- [x] `g5_closeout.ps1` 18 PASS / 0 FAIL / 0 SKIP（5 条子句各有专属断言）
- [x] 首轮的两处取数缺陷已修（共享读日志、`--list-encoders` 规范单行），非放宽判据
- [x] 覆盖表 5 条转「已实现」，总数 ≥99、无卡 ≤16
- [x] `docs/15` 裁定记录（轮 42 已裁定）落地，术语冲突不静默
- [x] `contract_check.py` / `card_consistency_check.py` / `domain_table_check.py` 通过
- [ ] 待验收方独立重跑（执行方不自评）

## 覆盖子句

- M8-01#1
- M6-10#1
- M6-11#1
- M3-15#1
- M6-04#2

## 交付状态

- M8-01#1: 已实现（轮 41；降级验证：12s 短录代理 5min 长录，loss=2.550s ≤10s）
- M6-10#1: 已实现（轮 41；zip/安装脚本/源码三方，驱动类条目 0）
- M6-11#1: 已实现（轮 41；三项指标 1Hz 刷新；码率/队列深度/写盘速度/编码器状态未呈现，已知限制）
- M3-15#1: 已实现（轮 41；代理验证：注入丢帧 ≠ 真实 GPU 压力，duration=5.983s）
- M6-04#2: 已实现（轮 41；按 `docs/03` 软编等价口径；x264 术语冲突已提 `docs/15` 裁定记录（轮 42 已裁定），裁定前验收方可判 PARTIAL）

> 已知限制与「哪些需求我没做完」见 `HANDOFF.md` 第四问。
