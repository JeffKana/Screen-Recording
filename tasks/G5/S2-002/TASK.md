# TASK: S2-002 窗口采集：跟随缩放（M1-08）+ 被遮挡内容（M1-09）

阶段：G5｜角色：S2｜轮次：36｜来源：验收方轮 36 指令 P1-2（首批 5 条无卡子句收口）

## 需求范围

| 需求 | 子句 | 验收要点原文（docs/01） | 证据形式 |
| --- | --- | --- | --- |
| M1-08 | #1 | 拖动窗口从 800×600 到 1280×720，成片画面始终完整包含窗口内容 | 成片前后段抽帧比对（窗口四角是否完整在画面内） |
| M1-08 | #2 | 宽高比变化时按「填充 + 黑边」策略处理且可配置为「拉伸」 | 两档策略各录一段，抽帧量黑边像素占比 |
| M1-09 | #1 | 用记事本遮挡浏览器后录制 10 秒，成片中浏览器内容完整可见、无遮挡物 | 成片抽帧与遮挡前基准帧比对 |

## 必读章节

- `docs/01-功能需求清单.md` M1-07~M1-09、M1-24
- `docs/02-系统架构.md` §4 数据流（窗口源的入口）、§7 接口契约
- `docs/03-技术选型.md` §4 采集选型（三条路径与回退顺序）、§10 许可
- `docs/05-非功能需求与验收.md` §3.2 显示与图形、§5 AC-01
- `CONTRACT.md` §4（`CaptureRequest` / `ICaptureSource` 契约）

## 现状（轮 36 代码普查结论，**未实现**的部分）

| 能力 | 现状 | 证据 |
| --- | --- | --- |
| 按窗口采集（能力层） | **有** | `src/capture/WgcCaptureSource.cpp:103-118`（`CreateForWindow`）；`src/capture/WindowCaptureSource.cpp:190-202`（每帧刷 bounds，尺寸变了重建纹理） |
| 按窗口采集（产品链路） | **无** | `src/recorder/RecordingSession.cpp:176-181` 硬编码 `kind = Display / Region`；recorder 无 `--window/--hwnd`；UI 无窗口选择（`src/app/main.cpp` 全文件 0 命中 `EnumWindows`） |
| 窗口缩放跟随（成片层） | **无** | 转换器/编码器/MKV 轨尺寸在开录时一次定死（`RecordingSession.cpp:203-212`、`:282-286`），窗口缩放后成片不跟随 |
| WGC 尺寸变化订阅 | **无** | 全仓 `ContentSizeChanged` 0 命中；尺寸变化 → `CopyResource` 失败 → `needs_rebuild` → 重建（异常驱动，非设计跟随） |
| 宽高比策略（填充+黑边 / 拉伸） | **无** | 全仓无 fit-mode 枚举/配置/CLI/UI；`Nv12Converter` 只做 1:1 blit（`Nv12Converter.cpp:216-224`） |
| 被遮挡可采（能力层） | **有** | `CaptureRequest.h:33-37` 的 `supports_occlusion`；WGC=true（`WgcCaptureSource.cpp:278`）、PrintWindow=true（`WindowCaptureSource.cpp:237-238`）、DXGI=false |
| 连续黑帧检测 + 提示 | **无** | `docs/03` §4 写了方案，代码 0 命中（`non_black` 只在 srprobe 自检里） |
| 验收用例 | **无** | `verify/cases/` 无缩放跟随 / 遮挡用例（`g2_walk.ps1:151-159` 只覆盖 M1-07 窗口非黑） |

## 文件域

- `src/capture/`、`src/compositor/`
- `verify/cases/g5_window.ps1`（新增）
- `tasks/G5/S2-002/*`

> **轮 40（F6 文件域归位）**：本卡原先声明了 `src/recorder/main.cpp` 与
> `src/recorder/RecordingSession.cpp`，但 `src/recorder/` 按 `CONTRACT.md` §2 属 **S1** 文件域
> → 属**双向越权**（S2 改了 S1 的目录）。轮 40 已把这两行从本卡文件域移除，
> 实际改动（`--window/--fit` 入口、`ComPtr` 持有上一帧纹理）改由 S1 的卡
> `tasks/G5/S1-008/TASK.md` 承接；本卡只保留 `src/capture/`、`src/compositor/`。
> `src/tools/srprobe/`（蓝框 + `--resize-cycle`）同样归 S1，一并由该卡承接。
>
> **需契约变更单**：`src/core/types/CaptureRequest.h` 要新增 `fit mode` 枚举与 `window_handle` 透传
> （`src/core/` 属 S1 文件域）。变更单在开工前提交 O0，未获批不得改 `src/core/`。

## 约束

- 不引三方依赖；WGC 仅 MSVC 构建可用（`src/capture/CMakeLists.txt:19-25`），MinGW 构建须走 PrintWindow 兜底。
- 窗口缩放时**不得**重建编码器（成片分辨率要稳定）：策略是「输出尺寸固定 + 按策略贴窗口内容」。
- 遮挡用例不许依赖记事本（Win11 打包应用取不到 `MainWindowHandle`，`g2_walk.ps1:151` 已踩过），
  必须用探针自建窗口 + 探针自建遮挡窗口。

## 完成定义（DoD）

- [ ] recorder 支持 `--window 0xNNNN`；UI 有窗口来源入口
- [ ] 窗口从 800×600 拖到 1280×720，成片前后段都完整包含窗口内容（黑边符合所选策略）
- [ ] `--fit pad`（填充+黑边）与 `--fit stretch`（拉伸）两档都可用且可从 CLI/配置选择
- [ ] 被遮挡窗口录 10s，成片内容完整可见、无遮挡物
- [ ] `verify/cases/g5_window.ps1` 三条断言全绿，MSVC + MinGW 都跑
- [ ] `docs/02` §7 / `CONTRACT.md` §4 同步（若开了契约变更单）

## 覆盖子句

- M1-08#1
- M1-08#2
- M1-09#1

## 交付状态

- M1-08#1: 已实现
- M1-08#2: 已实现
- M1-09#1: 已实现

> 轮 39：**三条全部已实现**（`verify/cases/g5_window.ps1` 24 PASS / 0 FAIL / 0 SKIP）。
>
> - **M1-08#1**（窗口尺寸跟随）：采集侧按帧的 `ContentSize()` 检测尺寸变化 → 重建帧池
>   （`IDirect3D11CaptureFramePool::Recreate`）+ 重建拷贝纹理；转换侧用
>   `VideoProcessorSetStreamSourceRect → SetStreamDestRect` 做缩放（**D3D11 的缩放就是这个映射**，
>   不存在 `VideoProcessorSetStreamScaling` 这个函数——轮 38 找错了函数名）。
>   实测：源尺寸变化 1 次、最终 1280×720、丢帧 0、切换点 5 帧无全黑、蓝色边框四边贴合（内容未被裁剪）。
> - **M1-08#2**（fill/stretch 两策略）：pad 等比缩放后上下黑边 **76px / 74px**（理论 75，±2px 内）；
>   stretch 拉伸铺满、黑边占比 **0.0%**。
> - **崩溃修复**（轮 38 的 0xC0000005 真缺陷）：采集侧重建纹理会释放旧纹理，而会话手里只有裸指针 →
>   会话改为用 `ComPtr` 持有上一帧纹理。实测**每秒切换一次尺寸、共 24 次，退出码 0、无崩溃、丢帧 0**。
> - **M1-09#1**（被遮挡）：绿色占比 95.9%、遮挡物红色 0.0%、蓝色边框贴合 ✓。
>
> **归因更正（轮 39）**：轮 38 报告把「没找到 `VideoProcessorSetStreamScaling`」写成「平台不支持缩放」，
> 并据此判定 M1-08 BLOCKED——**这是错的**：D3D11 里本就没有这个函数，缩放 API 是
> `SetStreamDestRect`（与源矩形配对）。同时轮 38 自设的护栏（源尺寸≠输出尺寸直接 `return false`）
> 把已经写好的 fill/stretch 分支挡成了不可达分支。两处均在轮 39 更正（详见 EVIDENCE §1 的 E1/E2）。

> 轮 36：本轮只完成**代码普查 + 卡 + 用例设计**，未实现。能力层（窗口采集、被遮挡可采）已存在，
> 缺的是「产品链路入口 + 成片层跟随 + 宽高比策略 + 验收用例」。
> 实现拆到轮 37~39（每轮 5 条子句，宁少勿多）。详见 `tasks/G5/S2-002/HANDOFF.md`。
