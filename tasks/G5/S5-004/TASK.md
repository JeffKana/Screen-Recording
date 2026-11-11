阶段：G5｜角色：S5｜轮次：40｜来源：验收方轮 40 指令 P0「无卡收口 ≥3 条」

## 需求范围

| 需求 | 子句 | 验收要点原文（docs/01） | 证据形式 |
| --- | --- | --- | --- |
| M8-03 | #1 | 剩余 < 2GB 时非阻塞提示、录制继续 | recorder 行为断言（disk_warn=1 / disk_stop=0 / record_exit=0）+ UI 状态栏与日志 |
| M8-03 | #2 | 剩余 < 500MB 升级为强提示 | UI 强提示 + 日志 disk_critical=1 + 反向对照（不误报） |
| M8-04 | #1 | 写满磁盘时空间耗尽前自动停止、成品完整无 0 字节 | recorder 行为断言（disk_stop=1 / 提前收尾 / 无 .part / 容器合法） |
| M8-05 | #1 | 按码率与剩余空间估算可录时长，误差 ≤ 20% | 估算值 vs 实测码率口径的相对误差 |

## 必读章节

- `docs/01-功能需求清单.md` M8-03 / M8-04 / M8-05
- `docs/05-非功能需求与验收.md` §6 ER-01~ER-04、§7.2
- `src/recorder/StorageGuard.h`（三级阈值：warn / critical / must_stop）
- `src/core/util/DiskEstimate.h`（剩余可录时长的唯一实现）

## 现状（轮 40 开工前普查）

| 能力 | 现状 | 证据 |
| --- | --- | --- |
| 剩余空间探测与可录时长估算 | **有** | `src/core/util/DiskEstimate.h`；`src/recorder/StorageGuard.cpp` |
| 非阻塞提示线（warn，2048MB） | **有** | `StorageGuard::Evaluate` → `stats.disk_warn` + `LogWarn("low disk: ...")`（`src/recorder/RecordingSession.cpp:1055-1060`） |
| 强提示线（critical） | **有状态、无消费者** | `StorageGuard::Status::critical` 被算出来但全仓无人读它；阈值还是 `warn/2`（1024MB），不是需求写的 500MB |
| UI 侧的磁盘提示 | **无** | `src/app/main.cpp` 的 `RefreshDiskRemaining` 只算剩余秒数，不判水位 |
| 验收断言 | **无** | `verify/cases/g3_storage.ps1` 覆盖 M8-04#1；M8-03#1/#2、M8-05#1 没有断言（M8-05 只断言了 `estimate-positive`） |

> **口径更正（轮 40）**：覆盖表此前把四条记为「已实现并经 g5_fallback / g3_storage 验证；缺 TASK.md 追溯，待补卡」。
> 逐条核对代码后：**M8-03#2 实际未实现**（critical 状态无人消费、阈值也不是 500MB），
> 其余三条的实现成立但缺断言。本卡补齐实现缺口 + 四条断言。

## 文件域

- `src/app/main.cpp`（磁盘水位两级提示、状态栏呈现）
- `src/app/AppConfig.h`、`src/app/AppConfig.cpp`（`disk_warn_mb` / `disk_critical_mb` 配置项）
- `verify/cases/g5_storage.ps1`（新增）
- `tasks/G5/S5-004/*`

> recorder 侧（`src/recorder/`）本轮**不改**：warn/stop 行为与估算公式已在既有实现里，
> 本卡只补 UI 呈现与断言。`src/recorder/` 属 S1 文件域。

## 约束

- 不引三方依赖；不改 `src/recorder/`、`src/core/`（跨域）。
- 提示**不得**阻断录制（M8-03#1 的「非阻塞」是硬要求）。
- 提示线做成配置项是为了让「触发/不触发」都能在不改磁盘的条件下验收；
  默认值必须等于需求口径（2GB / 500MB）。

## 完成定义（DoD）

- [x] M8-03#1：非阻塞提示触发时录制继续（record_exit=0、产物落盘）
- [x] M8-03#2：剩余不足强提示线时给出**强提示**（文案与提示线可区分），空间充足时不误报
- [x] M8-04#1：空间耗尽前自动停止、产物完整无 0 字节、无 .part 残留
- [x] M8-05#1：可录时长估算与实测码率口径的相对误差 ≤ 20%
- [x] `verify/cases/g5_storage.ps1` 13 PASS / 0 FAIL
- [x] 覆盖表四条子句转「已实现」，挂账理由清空

## 覆盖子句

- M8-03#1
- M8-03#2
- M8-04#1
- M8-05#1

## 交付状态

- M8-03#1: 已实现
- M8-03#2: 已实现
- M8-04#1: 已实现
- M8-05#1: 已实现
