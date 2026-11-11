# TASK: S6-003 轮 36 · 尾部音频补齐（R1）+ 解码判据按裁定改（R2）+ G5 登记关闭

阶段：G5｜角色：S6｜轮次：36｜来源：验收方轮 36 指令（`.codebuddy/plans/轮36指令.md`）P0 三项

## 需求范围

| 需求 | 子句 | 验收要点原文（docs/01） | 证据形式 |
| --- | --- | --- | --- |
| M6-09 | #1 | 8 小时不间断录制后文件完整可播、音画同步无累积漂移（≤ 100ms）、内存与句柄数稳定 | 8h 产物用新判据复判 + 60s/10min 尾点偏差实测 + 红绿对照 |
| M6-02 | #1 | 8 小时录制内存峰值与 10 分钟时相差 ≤ 50MB，无持续爬升趋势 | 同上（回归项） |

**本卡的实质工作**（轮 35 验收方 R1/R2/R3 三条 finding 的修正）：

| finding | 现象 | 本卡动作 |
| --- | --- | --- |
| **R1 [Required]** | 尾点偏差 99ms 只差 1ms 判据阈值；机制上界 186ms，重跑可能 120ms → FAIL | 收尾段按视频轨末尾 PTS 补齐音频（真实音频优先 + 补静音）；判据改为「≤100ms **且** ≤3×内部最大」 |
| **R2 [Required]** | `full_decode` 仍是「stderr 非空即 FAIL」，8h 被记成 FAIL | 按轮 35 裁定改成三件套（`-vsync 0` + framecrc DTS 递增 + 帧数比对）；③ 类警告不计 |
| **R3 [Nit]** | `summary.drift_points` 缺第 16 点（结尾点） | 结尾点进明细数组（并把采样间隔参数化，短录制也能出点） |

## 必读章节

- `docs/05-非功能需求与验收.md` §4.2（本轮新增的 M6-09 判据注释）、§3.1、§4
- `docs/01-功能需求清单.md` M6-02、M6-09
- `docs/07-执行台账.md` 轮 35 行（验收方已写入）
- `.codebuddy/plans/验收报告-轮35.md` §2（DEFECT-02 裁定）、§4（R1~R4）
- `.codebuddy/plans/轮36指令.md` §2、§3、§5

## 文件域

- `src/recorder/RecordingSession.h` / `RecordingSession.cpp` / `main.cpp`（R1 落点）
- `verify/cases/g5_lt01_real.ps1`（R2/R3 落点）
- `tasks/G5/S6-003/*`（本卡交付包）
- `docs/05`（M6-09 判据注释）、`docs/07`（台账）、`docs/09`（覆盖表，由生成器写）
- `verify/artifacts/lt01-real-*`（运行产物）

> **文件域缺口上报**：`src/recorder/` 未出现在 `docs/06` §2 的文件域归属表中。
> 本卡按 R1 的实际落点声明 `src/recorder/`，并在 HANDOFF「已知限制」里登记，
> 请 O0 在下一轮把 `src/recorder/` 明确划给 S1 或 S4，避免后续轮次各自解读。

## 上游交付

- 轮 35 的 8h 单文件产物：`verify/artifacts/lt01-real-20261027-184702/lt01-8h.mkv`
  （22,374,134,176 B，SHA256 `4811AA48…DC7BC9`；recorder 镜像 SHA256 `56295C2A…C10F7`）
- 轮 35 构建：`build-msvc/bin/Release`（MSVC 17.14，Release）

## 约束

1. **R1 是产品行为改动**（音轨变长），必须回归：暂停/恢复中断点（`g4_control`）、
   `.part` 恢复后的时长（`g3_recovery`）、CBR/VBR 音频码率统计（`g5_format`）。
2. **不得改判据阈值迁就实现**：需求就是尾点 ≤100ms 且 ≤3×内部最大。
3. **不得回填旧 8h 产物**：R1 的加固只对新录制生效；8h 产物用于「新判据复判」，
   不用于「证明 R1 已修」。
4. 重跑 8h 只在必须时做；能两级验证 + 理论补算覆盖就写明理由。
5. `verify/cases/` 下的用例可按验收方裁定改判据，但**只能改判据口径，不能放宽数值阈值**；
   任何改动必须在用例注释里写明裁定的出处。

## 完成定义（DoD）

- [ ] R1：收尾补齐落地，`audio_tail_fill_frames` / `audio_tail_silence_frames` 可从 stdout 读到
- [ ] R1 红绿对照：新断言在旧构建上 FAIL（实测值已记录）、在新构建上 PASS
- [ ] R1 尾点偏差：60s 与 10min 两级验证 ≤100ms 且 ≤3×内部最大；连跑 3 次不贴阈值
- [ ] R2：新判据三件套落地，8h 产物复判 PASS（命令与输出可重跑）
- [ ] R3：`summary.drift_points` 含结尾点
- [ ] 回归：`g4_control`、`g3_recovery`、`g5_format`、`g5_lt01_real` 零失败
- [ ] 双构建（Release + Debug）零告警
- [ ] G5 登记关闭：覆盖表 87/0/29，`coverage_report --check` 通过，契约 C1~C8 8/8
- [ ] 交付包四件套齐全（EVIDENCE / SELFTEST / HANDOFF）

## 覆盖子句

- M6-09#1
- M6-02#1

## 交付状态

- M6-09#1: 已实现
- M6-02#1: 已实现

> 轮 36：R1 已落地并红绿验证；R2 按轮 35 裁定改判据并用 8h 产物复判 PASS；
> G5 的登记关闭三步（交付状态 → `coverage_report --write` → 台账）已执行。
> 证据见 `tasks/G5/S6-003/EVIDENCE.md`。
