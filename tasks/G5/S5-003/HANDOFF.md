# HANDOFF: S5-003（M1-27）

## 1. 我改了什么接口？

**UI 内部新增（无跨模块契约变更）**：

| 项 | 说明 |
| --- | --- |
| `src/app/Elevation.h`（新增） | `IsProcessElevated()`（`OpenProcessToken` + `TokenElevation`）、`BuildElevatedParameters/CommandLine()`、`LaunchElevated()`（`ShellExecuteW(runas)`）、`ElevationLaunch` 枚举 |
| `src/app/main.cpp` | 启动时记 `uac_elevation_state`；自检新增 `elevation` 项（**非阻断**）；录制中记 `uac_warning phase=recording`；主窗口新增控件 210（提示标签）与 209（提权按钮）；托盘新增「以管理员身份重启…」；`WM_COMMAND` 接按钮 |
| `src/app/AppConfig.*` | 无（提权不需要配置） |

**IPC 协议、契约、CLI 语义均未变。** 唯一的 CLI 增量是 `--elevate-dry-run` / `--elevate-launch`
两个**仅在 `SR_TEST_HOOKS` 构建存在**的验收钩子（发布包 `--help` 里没有，见 `build_release.ps1` 的白名单自检）。

## 2. 下游需要接什么？

| 下游 | 要做什么 |
| --- | --- |
| 验收（V1） | 重跑 `verify/cases/g5_uac.ps1`；**注意有 1 条 SKIP**（黑屏一侧本机不可复现），读 `EVIDENCE.md` §E3 的「缺什么」 |
| 验收（V2） | 若要闭环黑屏一侧：需要一台 `PromptOnSecureDesktop=1` 的机器 + 人工确认 UAC 取样（步骤见 EVIDENCE §E3） |
| 后续轮次 | 若要做「自动拉起并交接」（提权后自动停旧实例、迁移配置），另开 S5 卡；本卡**故意不做**（用户没确认就被换进程） |

## 3. 已知限制是什么？

1. **黑屏一侧未闭环**（本机不可复现，详见 EVIDENCE §E3）。
2. 提示**常驻**（不只开录前弹一次），代价是主窗口多一行字。
3. 提权实例与原实例并存，不自动交接。
4. 提权实例写独立日志 `<log>.elevated`。
5. 提权检测只在**进程启动时**做一次（进程提权状态在生命周期内不变）。

## 4. 哪些需求我**没**做完？

| 需求 | 状态 | 说明 |
| --- | --- | --- |
| M1-27#1 | **有卡未实现**（不是「已实现」） | 三个验收点里 ②提示、③入口 **已交付并有证据**；①「成片该窗口区域为黑屏」**本机不可复现**（实测证伪：WGC 采到真实内容，黑帧 1.0%），缺「UAC 落安全桌面」的机器 + 人工确认取样 |

**我没有把 M1-27 标成「已实现」。** 覆盖表里它仍是「有卡未实现」，
挂账理由写明「黑屏一侧环境不可复现，缺什么见 EVIDENCE §E3」。
