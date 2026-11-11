# EVIDENCE: S1-001 core 抽象层与状态机

> 自测命令与输出即自测记录（SELFTEST 与 EVIDENCE 合并，避免同一份证据写两遍）。

复现环境：Windows 11 (10.0.26200) / MinGW-w64 GCC 16.2.0 / CMake 4.4.3 / Ninja。

## M6-13 core 层无平台依赖、接口可替换

- 验证方式：静态扫描 `src/core/` 全部源文件的 `#include`，比对 `CONTRACT.md §3` 的禁止关键字；
  并校验 `src/core/interfaces/` 与契约声明的 8 个接口一一对应。
- 命令：`python verify/tools/contract_check.py`
- 输出：

```
[PASS] C3 core 层无平台依赖 (M6-13) — 扫描 17 个 core 文件, 违规 0 处
[PASS] C4 core 接口齐备 (CONTRACT §4) — 声明 8 个, 实际 8 个
```

- 结论：通过。平台能力（QPC 时钟）落在 `src/platform/win/QpcClock.cpp`，core 只依赖 `IClock`。
- 补充证据：编译产物验证 `-Wall -Wextra` 零告警，说明抽象层无隐式类型/未用参数问题。

## M2-10 状态机六态互斥、重复命令幂等、非法命令被拒

- 验证方式：跨进程驱动状态机走完全部 15 条转移，并插入两类反向断言。
- 命令：`pwsh -NoProfile -File verify/cases/g1_walk.ps1 -BuildDir build-mingw`
- 关键输出（节选）：

```
--- T01/T03: Idle -> Preflight -> Idle ---
  PASS start            -> Preflight
  PASS preflight-fail   -> Idle
--- illegal command: resume in Idle must be rejected, state unchanged ---
  PASS resume           -> Idle
--- T14/T15: crash and reset ---
  PASS crash            -> Crashed
  PASS reset            -> Idle
--- M2-10: duplicate start while recording must be idempotent ---
  PASS start            -> Recording
RESULT: 40 PASS / 0 FAIL
```

- 结论：通过。15 条转移全覆盖；`resume` 在 `Idle` 被拒且状态不变；`Recording` 下重复 `start` 幂等。
- 静态佐证：`contract_check.py` 的 C5 比对契约 15 条 / 代码 15 条，条目数一致。

## M2-16 录制中关键参数锁定

- 验证方式：在 `Recording` 状态下读取服务端推送的状态快照字段 `parameter_locked`。
- 命令：`srctl --pipe \\.\pipe\sr.recorder.<pid> state`（在录制态下调用）
- 输出：`state=Recording parameter_locked=true elapsed_ms=0`
- 结论：通过。`Idle` / `Preflight` / `Countdown` / `Stopping` / `Crashed` 均为 `false`，
  仅 `Recording` / `Paused` / `Recovering` 为 `true`，与 M2-16「音量类参数不锁定」的边界一致
  （音量不在状态机管辖范围内）。

## 汇总

| 需求 | 结论 | 证据类型 |
| --- | --- | --- |
| M6-13 | PASS | 静态扫描 + 编译 |
| M2-10 | PASS | 运行时遍历 + 静态条目数比对 |
| M2-16 | PASS | 运行时快照字段 |
