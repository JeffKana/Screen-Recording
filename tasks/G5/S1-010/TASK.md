# TASK: S1-010 轮 42 · M6-08#2 CPU 中转路径的可验证入口

阶段：G5｜角色：S1（录制进程 / 合成器）｜轮次：42｜来源：`.codebuddy/plans/轮42指令.md` §1 P0

## 需求范围

| 子句 | 验收要点原文（`docs/01:203`） |
| --- | --- |
| M6-08#2 | 如无法避免则走 CPU 中转仍能录制成功 |

## 现状（开工普查）

- `src/compositor/Nv12Converter.cpp:492-519`：`Convert()` 在 `gpu_available_` 为假或运行期 GPU 转换失败时，
  走 `ConvertOnCpu()` 兜底；`:505-508` 运行期失败会置 `force_cpu_=true` 并 `Resize()` 到 CPU staging。
- `Nv12Converter.h:92` `bool force_cpu_ = false`（私有），`:76` `Resize()` 以它决定是否建 GPU 资源。
- `src/recorder/RecordingSession.cpp:211-216`：初始化后把 `converter.UsingGpu()` 写进 `stats.converter`
  （`gpu-videoprocessor` / `cpu`）。
- **缺口**：CPU 中转能力已实现，但只能靠「运行期 GPU 转换失败」被动触发，无稳定、可重跑的触发入口。

## 文件域（白名单）

- `src/recorder/main.cpp`、`src/recorder/RecordingSession.h`、`src/recorder/RecordingSession.cpp`
- `tasks/G5/S1-010/*`（本卡四件套）

> `src/compositor/` 属 `CONTRACT.md §2` 的 **S2** 域，由同轮 `tasks/G5/S2-003` 独占承接
> （只加 `Nv12Converter::ForceCpu()` 这个可选入口）；本卡不越域。
> 与 `tasks/G6/V1-003`（verify/ + docs/）、`tasks/G5/S5-005`（src/app/）、
> `tasks/G5/V1-005`（状态迁移）目录互斥。不改 `CONTRACT.md`（无接口签名变更）。

## 上游交付

- 轮 39 S1-008 卡（该文件域历史归属）、`build-msvc/bin/Release` 与 `build-release/bin/Release`
- `verify/tools/build_release.ps1` 的 `$debugSwitches` 白名单（新增开关必须落在发布构建之外）

## 约束

1. `--force-cpu` 只在 `SR_TEST_HOOKS=ON` 的开发/验收构建可用；发布构建 `OFF` 时解析段与 help 都不出现，
   `build_release.ps1` 的 `release-help-no-debug-switch` 保持 PASS。
2. 只让**既有** CPU 路径可达，不新写转换算法、不改 `CONTRACT §4` 接口、不引三方依赖。
3. 证据必须标明：强制 CPU 是**触发手段**，CPU 中转本身是既有产品能力；属降级验证（非真实 GPU 故障）。
4. 接口无签名变更，无需契约变更单。

## 完成定义（DoD）

- [ ] `--force-cpu` 让 `converter=cpu`、`record_exit=0`、成片 video+audio 可解码
- [ ] 对照组（不传开关）`converter=gpu-videoprocessor`，证明开关确实切换了路径
- [ ] 发布包 help 不含该调试开关；三构建零告警
- [ ] 四件套齐全

## 覆盖子句

- M6-08#2

## 交付状态

- M6-08#2: 已实现（轮 42；`--force-cpu` 令既有 CPU 中转路径可达，实测 `converter=cpu` + 录制成功 + 成片可解码；降级验证：强制开关触发，非真实 GPU 故障）
