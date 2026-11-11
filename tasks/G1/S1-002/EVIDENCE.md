# EVIDENCE: S1-002 IPC 控制通道与 Recorder 进程骨架

## M2-01 命令下发时序（进入录制态）

- 验证方式：`srctl` 作为客户端完成握手后连续下发 `start → preflight-ok → countdown`，
  服务端每次回复状态快照；统计单次命令往返耗时。
- 命令：`pwsh -NoProfile -File verify/cases/g1_walk.ps1 -BuildDir build-mingw`
- 输出（节选）：

```
recorder pid=37980 pipe=\\.\pipe\sr.recorder.37980
--- T04/T06/T07: record, pause, resume ---
  PASS start            -> Preflight
  PASS preflight-ok     -> Countdown
  PASS countdown        -> Recording
  PASS pause            -> Paused
  PASS resume           -> Recording
```

- 结论：通过（骨架级）。三次命令往返在本地管道上远低于 M2-01 的 1.5s 上限；
  真实端到端时延须在 S2/S4 接入后按 05 §2.2 的「点击开始 → 录制态」重新测量，
  本阶段不能以此替代 M2-01 的完整验收。

## M2-10 非法命令不改变状态、不导致断连

- 验证方式：在 `Idle` 下下发 `resume`，断言出现 `rejected:` 且状态仍为 `Idle`，
  且后续命令仍可正常执行（说明连接未被动断开）。
- 输出：

```
--- illegal command: resume in Idle must be rejected, state unchanged ---
  PASS resume           -> Idle
```

- 结论：通过。拒绝路径回 `ErrorEvent` 而非断开，符合 02 文档 §7.1 的错误总线约定。

## M2-12 单实例

- 验证方式：首实例运行中启动第二个 `sr-recorder`，读取退出码与标准输出。
- 输出：

```
PASS second instance exit=4 msg=sr-recorder already running, pid=3600
```

- 结论：通过。第二实例退出码 4，并能报告首实例 pid。
- 备注：此处修复了一处真实缺陷（见台账 D-02）。

## M8-14 版本与诊断信息

- 命令：`.\build-mingw\bin\sr-recorder.exe --version`
- 输出：`sr-recorder 0.1.0 (ipc protocol v1, core state table 15 transitions)`
- 结论：部分通过。版本、协议版本、状态机条目数可输出；硬件信息与日志打包属 S6 的
  `src/diag/`，不在本任务卡范围，M8-14 整体不在此判定通过。

## 优雅关闭

```
--- graceful shutdown ---
  PASS shutdown exit=0
```

## 汇总

| 需求 | 结论 | 说明 |
| --- | --- | --- |
| M2-01 | PARTIAL | 骨架时序通过，端到端时延待 S2/S4 后复测 |
| M2-10 | PASS | 拒绝不回退、不断连 |
| M2-12 | PASS | 退出码 4 + pid 提示 |
| M8-14 | PARTIAL | 版本可输出，诊断包归 S6 |
