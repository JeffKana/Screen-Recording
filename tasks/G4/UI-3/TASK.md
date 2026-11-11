# TASK: UI-3 开录前自检、区域框选/微调、G4 关闭

## 元信息

- 角色: S5（应用服务与界面），协作 V1（自动化验收）
- 阶段: G4（轮 29；G4 门关闭）
- 需求锚点: **M2-14**（开录前自检）、**M1-04**（区域框选）、**M1-05**（区域微调）
- 上游依赖: UI-1/UI-2、Recorder 的 `--region`（本次新增）、D-39 媒体时间
- 计划轮次: 1（轮 29）

## 文件域

| 文件 | 归属 | 说明 |
| --- | --- | --- |
| `src/app/main.cpp` | S5 | 自检、区域覆盖层/编辑器、DPI、状态栏 |
| `src/app/AppConfig.*` | S5 | 区域持久化 + 偶数对齐 |
| `src/app/RecorderController.*` | S5 | 传递 `--region` |
| `src/recorder/main.cpp`、`RecordingSession.*` | S1 | Recorder 接收 `--region`，按 Region 采集 |
| `src/capture/CaptureDeviceAccess.cpp`、`RegionCaptureSource.h` | S2 | 区域源暴露内层 D3D11 设备 |
| `src/tools/srctl/main.cpp` | S2 | 旧服务端快照退化（F1） |
| `verify/cases/ui_smoke.ps1`、`g4_gate.ps1`、`stimulus.ps1` | V1 | I/J 段 + 60s 门 |
| `docs/05`、`docs/07`、`docs/08` | O0/V1 | 口径、台账、G4 结论 |

## 覆盖子句

- M2-14#*
- M1-04#*
- M1-05#*

## 需求范围与判据

| 需求 | 判据 | 版本 |
| --- | --- | --- |
| M2-14 | 磁盘/显示器/音频端点/编码器四项自检，各自失败 → 阻止开录 + 原因可见；≤500ms；不留 `.part` | V1.0 |
| M1-04 | 框选选区 = 成片分辨率（±1px）；801×601 → 800×600；物理像素 + `GetDpiForWindow` | V1.0 |
| M1-05 | 比例预设误差 ≤1px；方向键步进 1px（Shift 10px） | V1.0 |

## 约束

- 500ms 预算不能靠 spawn Recorder（启动就 ~450ms）：UI 做快检（磁盘/显示器/音频端点/编码器缓存），
  真失败由 exit 6/12/13 兜底回传。
- 区域框选必须 `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)` + `GetDpiForWindow` + 物理点；
  宁可显式记录 DPI，也不要用缩放后的坐标。
- 偶数对齐只在 UI 做（宽高 `& ~1`），Recorder 不再二次修正；x/y 保持 1px 精度。
- G4 关闭逐条对 05 判据；缺一条就写「有条件」并挂账。

## 完成定义

- `ui_smoke.ps1` 含 I/J 段全绿（区域 801×601→800×600、编辑器 16:9、框选拖拽、四项自检失败阻止）。
- `g4_gate.ps1` 60s 素材 12/12：停止到可播 ≤3s、重复帧 ≤1%、无残留。
- `g4_channel / g4_elapsed / g4_control / g5_* / g3_*` 零回归。
- `contract_check.py` 8/8；`coverage_report --check` 一致。
- 文档：05 §2.3（抖动/重复帧口径）、07 轮 29、08 §23、README 的 G4 状态。

## 交付状态

> 轮 32（F2）补节：覆盖模型默认值已翻成「有卡未实现」，历史卡必须显式声明。
> 以下状态与 `docs/09` 当前结论一致（门已关闭且证据在对应轮次的台账里）。

- M1-04: 已实现
- M1-05: 已实现
- M2-14: 已实现
