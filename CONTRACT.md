# CONTRACT · G1 冻结契约

> 由 O0 在 G1 门开工前冻结。**任何对本文件的修改都必须走契约变更单**（见 [06-子智能体执行规程 §5.4](./docs/06-子智能体执行规程.md)）。
> 本契约服务于 G1（S1 基座），不包含采集/编码/UI 的业务接口实现。

## 1. 构建契约

| 项 | 值 |
| --- | --- |
| 标准 | C++20（`/std:c++20`），异常关闭能力后续按需开启 |
| 编译器 | MSVC 2022（v143 工具集）+ Windows SDK 10.0.22621 及以上 |
| 构建系统 | CMake ≥ 3.25，生成器 Visual Studio 17 2022（x64） |
| 依赖管理 | vcpkg manifest 模式，`vcpkg.json` 在 G1 阶段依赖为空 |
| 输出目录 | `build/<Config>/`，可执行文件命名为 `sr-recorder.exe`、`srctl.exe` |
| 第三方依赖 | **G1 阶段禁止引入任何三方依赖**（vcpkg.json 只读） |

标准构建命令（全组统一，不得各自发明）：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## 2. 文件域归属（互斥，不得越界）

| 角色 | 目录 | 说明 |
| --- | --- | --- |
| S1 | `src/core/`、`src/ipc/`、`src/platform/`、`src/recorder/`、`src/tools/` | 基座、进程骨架、诊断探针 |
| S2 | `src/capture/`、`src/compositor/` | 采集与合成 |
| S3 | `src/audio/` | WASAPI 与混音 |
| S4 | `src/encode/`、`src/mux/` | 编码与封装 |
| S5 | `src/app/`、`ui/` | 应用服务与界面 |
| S6 | `src/resilience/`、`src/diag/` | 恢复、背压、诊断 |
| V1 | `verify/` | 验收用例与核验工具 |

**根目录文件**（`CMakeLists.txt`、`vcpkg.json`、`CONTRACT.md`）由 O0 维护，S 系列角色只读。

## 3. 分层依赖规则（对应 M6-13）

```
src/core/      只允许依赖 C++ 标准库。禁止 include 任何平台头
src/ipc/       允许 Windows API（管道）。禁止 include D3D/音频/编码头
src/platform/  允许 Windows API，向上暴露 std 类型的抽象接口
src/recorder/  可依赖 core + ipc + platform
src/capture|audio|encode|mux/  可依赖 core + platform，禁止互相依赖
src/app/       可依赖 core + ipc，禁止直接依赖 capture/audio/encode/mux
```

`src/core/` 的禁止 include 关键字（由 `verify/tools/contract_check.py` 机器校验，对 include 路径做小写子串匹配）：

`windows`、`d3d`、`dxgi`、`audioclient`、`mmdevice`、`mfapi`、`mfreadwrite`、`wrl/`、`winrt/`、`qt`

## 4. core 接口清单（8 个，缺一即 G1 不通过）

| 接口 | 职责 | 需求锚点 |
| --- | --- | --- |
| `ICaptureSource` | 屏幕/窗口采集，产出带时间戳的 GPU 纹理引用 | M1-01~10 |
| `IAudioSource` | 音频采集，产出 float32 交错样本块 | M1-15、M1-17 |
| `IAudioMixer` | 多路混音、增益、限幅、静音填充 | M1-18、M1-20、M1-21 |
| `ICompositor` | PiP/水印/遮挡/光标合成 | M1-12、M7-06、M7-09 |
| `IVideoEncoder` | 纹理进、编码包出 | M3-05、M3-06 |
| `IAudioEncoder` | 样本进、编码包出 | M3-12 |
| `IMuxer` | 按 DTS 写包、切分片 | M5-04、M2-11 |
| `IRecoveryService` | 分片探测与 remux 修复 | M8-01、M8-02 |

## 5. 状态机契约（唯一权威转移表）

状态 8 个：`Idle`、`Preflight`、`Countdown`、`Recording`、`Paused`、`Stopping`、`Recovering`、`Crashed`
命令 14 个：`Start`、`PreflightOk`、`PreflightFail`、`CountdownElapsed`、`Cancel`、`Pause`、`Resume`、`Stop`、`FinalizeDone`、`Recover`、`RecoverOk`、`RecoverFail`、`Crash`、`Reset`

| # | From | 命令 | To | 说明 |
| --- | --- | --- | --- | --- |
| T01 | Idle | Start | Preflight | M2-14 自检 |
| T02 | Preflight | PreflightOk | Countdown | 自检通过 |
| T03 | Preflight | PreflightFail | Idle | 自检失败并报错 |
| T04 | Countdown | CountdownElapsed | Recording | M2-04 |
| T05 | Countdown | Cancel | Idle | 取消不产生文件 |
| T06 | Recording | Pause | Paused | M2-02 |
| T07 | Paused | Resume | Recording | M2-02 |
| T08 | Recording | Stop | Stopping | M2-03 |
| T09 | Paused | Stop | Stopping | M2-03 |
| T10 | Stopping | FinalizeDone | Idle | 封装完成 |
| T11 | Recording | Recover | Recovering | 采集卡死/TDR |
| T12 | Recovering | RecoverOk | Recording | M8-09 |
| T13 | Recovering | RecoverFail | Stopping | 保存已录部分 |
| T14 | Recording | Crash | Crashed | 进程异常 |
| T15 | Crashed | Reset | Idle | 重启后走恢复流程 |

**未列出的 (状态, 命令) 组合一律拒绝**，返回 `Rejected` 与原状态不变。重复下发同一命令必须幂等返回当前状态，不得报错。

## 6. 错误码表

`None`、`EncoderInitFailed`、`EncoderRuntimeFailed`、`CaptureStartFailed`、`CaptureLost`、`CaptureTimeout`、`AudioDeviceLost`、`DiskSpaceLow`、`DiskFull`、`PathNotWritable`、`FileLocked`、`GpuDeviceLost`、`UnsupportedSource`、`RecoveryFailed`

严重级：`Warn`（仅记录）、`Recoverable`（自动恢复并 toast）、`Fatal`（走 Stopping 收尾）。

## 7. IPC 协议

| 项 | 值 |
| --- | --- |
| 载体 | 命名管道，`\\.\pipe\sr.recorder.<pid>` |
| 分帧 | `magic(4) + version(2) + type(2) + payloadLen(4)` 固定 12 字节头，小端 |
| magic | `0x53455243`（"SERC"） |
| version | 1 |
| 消息类型 | `Hello=1`、`HelloAck=2`、`Command=3`、`StateSnapshot=4`、`ErrorEvent=5`、`Shutdown=6` |
| 单帧上限 | 1 MiB，超限直接断开连接 |
| 握手 | 客户端先发 `Hello{token}`，服务端校验 token 后回 `HelloAck{protocolVersion, processId}`，并**紧跟一条当前 `StateSnapshot`**；校验失败的服务端立即断开 |
| 握手时限 | 连接建立后 1s 内未收到合法 `Hello` 一律断开，记 `control=rejected` |
| 握手时序 | 握手在主循环中以**非阻塞状态机**推进（候选连接 + 截止时间），静默客户端不阻塞录制；断开后录制继续，暂停中则见 §18.3 G4-C9 |
| 快照推送 | 握手后紧跟一条当前 `StateSnapshot`；**录制中至少每秒一条**；`kind=0` 为周期/初始快照，`kind=1` 为命令回执。`srctl` 只把 `kind=1` 当命令结果，避免排队中的周期快照被误读 |
| 载荷兼容 | `StateSnapshot` 末尾新增 1 字节 `kind`；旧 10 字节载荷按 `kind=0` 解析，协议版本保持 1 |
| 旧服务端行为兼容 | `srctl` 在命令后等待 300ms 仍无 `kind=1` 时，退化为旧行为：打印最后一条 `kind=0` 快照并输出 `legacy_snapshot_fallback`，不再静默返回 0 |
| 快照扩展块（轮 30 / M6-03） | base 11 字节之后追加 `ext_bytes(u16) + ext 载荷`；`ext` 首字段是 `ext_version(u16)`，v1 依次为 `capture_fps_x100(u32) / encode_fps_x100(u32) / frames_dropped(u64) / frames_captured(u64)` | 
| 扩展块兼容 | 旧客户端按 `Reader::Remaining()` 整块跳过；新客户端读到 `ext_version ≥ 1` 才解析已知字段，其余字节忽略。协议版本仍为 **1**，`§7` 帧格式不变 |
| v2 计划 | 本轮是第二次向快照追加字段（前一次是 `kind`）。**T-18 前置项**：协议升 v2 时把 `state/parameter_locked/elapsed/kind/ext` 收敛为「字段表 + 类型长度」自描述载荷，并给出 `srctl`/UI 的版本协商；在 v2 落地前，只允许通过 `ext_version` 加字段，不得再改 base 布局 |

> **区域参数（M1-04）**：Recorder 的 `--region x,y,w,h` 使用**物理像素**；`w/h` 必须是偶数
> （UI 已按 `& ~1` 对齐），`x/y` 允许奇数。UI 传入的选区就是成片分辨率，Recorder 不再二次修正。
> `srprobe` 的 `capture --source region` 与 Recorder 走同一条 `CaptureKind::Region` 路径。

## 8. G1 退出判据（本文件的验收对象）

| # | 判据 | 当前状态 | 证据 |
| --- | --- | --- | --- |
| E1 | core 抽象层 8 个接口齐备且无平台依赖 | **PASS** | `contract_check.py` C3 / C4 |
| E2 | 状态机 15 条转移可遍历，非法组合被拒 | **PASS** | `g1_walk.ps1` 40/40，含反向断言 |
| E3 | 双进程可通过命名管道完成握手 | **PASS** | `g1_walk.ps1` 握手 + 命令往返 |
| E4 | Release + Debug 编译零错误 | **PASS** | MSVC 14.44 Release/Debug 零错误零告警；MinGW GCC 16.2 亦通过 |
| E5 | `contract_check.py` 全项 PASS | **PASS** | 8 项检查全绿（C7 覆盖非空 + C8 未切片逐条挂账） |

> **E4 关闭说明**：VS Build Tools 2022（MSVC 14.44.35207）与 Windows SDK（安装在
> `D:\Windows Kits\10`，含 10.0.22621/26100）已就位。Release 与 Debug 两种配置均零错误
> 零告警；MSVC 产出的 MKV 经独立解析器 0 failure、ffmpeg 解码 180 帧零错误，
> 与 MinGW 产物行为一致（台账 D-04 已关闭）。

> **覆盖口径（轮 27 / D-40）**：C1/C7 只统计 `tasks/**/TASK.md`，EVIDENCE/HANDOFF 里的
> 引用不算切片；C8 要求每条无卡的 V1.0 需求在 `docs/09-V1.0覆盖表.md` 的「挂账理由」列
> 逐条说明（空值/`—`/`待填` 视为未挂账）。覆盖表由
> `python verify/tools/coverage_report.py --write` 生成，`--check` 用于 CI 防漂移。

## 9. G2 判据（采集与音频链路）

| # | 判据 | 状态 | 证据 |
| --- | --- | --- | --- |
| G2-1 | 全屏/区域/窗口三类源均可连续产出帧 | **PARTIAL** | 三类源均可出帧；全屏吞吐待活跃会话复测（台账 D-09） |
| G2-2 | 区域尺寸与请求一致，奇数按 YUV420 偶数对齐 | **PASS** | 800×600 精确；801×601 → 800×600 |
| G2-3 | 窗口采集内容非空（非全黑） | **PASS** | 自建窗口回读：非黑 98.2%、绘制色命中 87.8% |
| G2-4 | 音频时间轴零漂移（M1-21） | **PASS** | 三轮 `expected == produced`，drift 恒为 0 |
| G2-5 | 混音增益/静音/限幅规则正确 | **PASS** | `mixselftest` 5/5 |
| G2-6 | 混合 DPI 下选区与画面一致 | **BLOCKED** | 参考机双屏均为 100% 缩放 |
| G2-7 | 默认音频设备变更可检出 | **BLOCKED** | 无设备切换手段 |

> G2 结论：**勉强达标（S=80.4）**。G2-1 的 PARTIAL 与两项 BLOCKED 均为环境或测量条件问题，
> 不涉及接口变更，因此 G1 结论不回退，S4 可并行开工。

## 10. G3 判据（编码与封装）

| # | 判据 | 状态 | 证据 |
| --- | --- | --- | --- |
| G3-1 | 采集 → NV12 → H.264 全链路产出文件 | **PASS** | 8 秒样本 240 帧、1.99 MB |
| G3-2 | 容器结构符合 Matroska 规范 | **PASS** | 独立解析器 13 项全 PASS，Duration 与末帧 PTS 一致 |
| G3-3 | 码流结构合法（avcC / SPS / PPS / IDR / AVCC 链） | **PASS** | avcC 41 字节；NAL 7/8/5/1/6/9；bad=0 |
| G3-4 | 硬编优先、软编确定性回退 | **PASS** | 回落 `mf-h264-sw`，录制不中断 |
| G3-5 | 用解码器/播放器回放成片 | **PASS** | ffmpeg 全解 180 帧零错误；采样帧平均色非空 |
| G3-6 | 三厂商硬编（NVENC/oneVPL/AMF） | **BLOCKED** | 缺厂商 SDK |
| G3-7 | 音频轨（AAC） | **BLOCKED** | 本卡未实现 M3-12 |
| G3-8 | 崩溃后分片可恢复 | **PARTIAL** | 6 个完整 Cluster；未做故障注入 |

> G3 结论：**勉强达标（S=79.1）**。解码回放补齐后功能项全绿；效率维度仍低（6 轮 vs 计划 2 轮，
> 其中 5 轮属自研 EBML 写入器缺陷）。已落地「写出即自校验」与「先解析器后写入器」两条约束，
> G4 起对二进制格式的改动必须先自校验通过再合并。

## 11. G3.5 判据（能力补齐）

| # | 判据 | 状态 | 证据 |
| --- | --- | --- | --- |
| G35-1 | WGC 可采集显示器 | **PASS** | `active_backend=wgc`，1920×1080，58.49fps，p99=31ms，零抖动 |
| G35-2 | WGC 可采集窗口（含被遮挡） | **PASS** | 窗口采集 1222×897，`occlusion_supported=1` |
| G35-3 | WGC 不可用时自动降级 | **PASS** | MinGW 构建 `chain_trace=wgc:fail → dxgi:ok`，录制不受影响 |
| G35-4 | MinGW 与 MSVC 均可构建 | **PASS** | MSVC `WGC ENABLED` / MinGW `DISABLED`，两者零错误 |
| G35-5 | MKV 含 AAC 音轨且可解码 | **PASS** | ffprobe `index=1 aac 44100 2ch`；ffmpeg 整文件解码零错误 |

> G3.5 结论：**达标**。WGC 通过 PIMPL + `SR_HAS_WGC` 隔离，MinGW 路径保留 DXGI 兜底；
> 音频链路 WASAPI 回环 → 时间轴 → AAC → MKV 全通。
> 注意：AAC MFT 要求**先设输入类型再设输出类型**，与视频编码器顺序相反（台账 D-19）。
## 12. G5 部分判据（崩溃恢复）

| # | 判据 | 状态 | 证据 |
| --- | --- | --- | --- |
| G5-R1 | 录制写临时文件，正常收尾才改名 | **PASS** | 正常录制无 `.part` 残留 |
| G5-R2 | 强杀后可扫描出可修复文件 | **PASS** | `--scan` 报 `repairable=1` |
| G5-R3 | 修复产物可解码 | **PASS** | ffmpeg 解码零错误，含 video+audio 轨 |
| G5-R4 | 丢失 ≤ 10 秒 | **PASS** | 6s 处强杀，恢复 3.436s，丢失 2.56s |

> 恢复采用「截断到最后一个完整 Cluster + 回填 Duration」，**不重编码**，因此秒级完成。
> 剩余 G5 判据（8 小时长录制、ER 故障注入矩阵）仍需执行（T-08）。
## 13. G5 存储与异常判据

| # | 判据 | 状态 | 证据 |
| --- | --- | --- | --- |
| G5-S1 | 剩余空间低于阈值时非阻塞提示且录制继续 | **PASS** | `disk_warn=1`，录制未中断 |
| G5-S2 | 空间耗尽前主动停止，成品可播 | **PASS** | 30s 请求在 9.07s 提前收尾，结构 0 failure、解码零错误 |
| G5-S3 | 不产生 0 字节 / 不可播文件 | **PASS** | 产物 2.7 MB，无 `.part` 残留 |
| G5-S4 | 剩余可录时长预估随空间与码率变化 | **PASS** | 202GB → 260898s；低于 stop 线时归零 |
| G5-S5 | 空间不足时开录即拒绝且不留碎片 | **PASS** | `record_exit=12`，无文件残留 |
| G5-S6 | 长时间录制稳定性（LT-01） | **暂缓** | 用户决定推迟，不得表述为已通过 |
## 14. G5 输出路径判据

| # | 判据 | 状态 | 证据 |
| --- | --- | --- | --- |
| G5-P1 | 同名文件不覆盖，自动追加序号 | **PASS** | `same.mkv` + `same_001.mkv` 并存且均可播 |
| G5-P2 | 目标被独占打开时自动改名而非失败 | **PASS** | 持锁录制成功，落地 `same_002.mkv` |
| G5-P3 | 目录不可创建/不可写时开录前报错 | **PASS** | `record_exit=13`，无碎片文件 |
| G5-P4 | 录制中权限被收回时切备用路径 | **未实现** | 留待 T-16 |

> **D-24（已关闭，2026-10-21）**：Windows 的 `std::filesystem::rename` 会覆盖已存在的目标
> （MSVC STL 带 `MOVEFILE_REPLACE_EXISTING`），旧实现「收尾改名失败再避让」在该分支上永不触发，
> 录制期间被他人创建的同名文件会被静默覆盖。现改为准备阶段与收尾共用 `FindFreePath`
> 主动避让；序号 `_001~_999` 全占时返回失败而非复用已存在路径。
> 证据：`tasks/G5/S1-001/EVIDENCE.md`，回归 `g3_output.ps1` 15/15。
## 15. G5 控制闭环判据

| # | 判据 | 状态 | 证据 |
| --- | --- | --- | --- |
> 锚点按 2026-10-21 裁定统一为 **M2-02 / M2-04 / M7-02**（原记 M2-03/04/05 有误）。

| # | 判据 | 锚点 | 状态 | 证据 |
| --- | --- | --- | --- | --- |
| G5-C1 | 暂停时长不计入成片 | M2-02 | **PASS** | 6s+4s 暂停 → 成片 5.999s / 180 帧 |
| G5-C2 | 暂停期间不写盘 | M2-02 | **PASS** | 封装字节数增长 0 B |
| G5-C3 | 恢复无长空洞（≤3 帧） | M2-02 | **PASS** | 最大帧间隔 34ms |
| G5-C4 | 支持多段暂停且状态机可自证 | M2-02 | **PASS** | 两段暂停，`state_sequence` 精确匹配 |
| G5-C5 | Stop 后 Pause 必须被拒 | M2-02 | **PASS** | `pause_after_stop_rejected=1` |
| G5-C6 | 倒计时后开录且不进成片 | M2-04 | **PASS** | countdown 3002ms，成片 2.999s |
| G5-C7 | 取消后不产生任何文件（含目录） | M2-04 | **PASS** | exit 14，目标目录未创建 |
| G5-C8 | 按时长自动停止 | M7-02 | **PASS** | 8s → 7.999s / 240 帧 |
| G5-C9 | 按结束时间停止 | M7-02 | **未实现** | 属 V1.5 |
| G5-C10 | 音视频末帧偏差 ≤300ms | — | **PASS** | 暂停 234ms / 非暂停 53ms |
| G5-C11 | 采集后端恢复后不漂移 | — | **PASS** | WGC / DXGI 均 `backend_changed=0` |
| G5-C12 | 运行期错误不得静默成功（D-28） | — | **PASS** | `--fail-resume` → exit 15，成品 1.999s 可播 |
| G5-C13 | 暂停/恢复可经 IPC 触发 | M2-02 | **未实现** | 登记为 T-09 前置 |
## 16. G5 故障注入判据（T-16a）

| # | 判据 | 锚点 | 状态 | 证据 |
| --- | --- | --- | --- | --- |
| G5-I1 | 磁盘耗尽前安全停止，成品可播、无 0 字节、无 `.part` | ER-04 | **PASS** | 3/3 一致，`disk_stop=1`、exit 0 |
| G5-I2 | 路径不可写时开录前拒绝且不留碎片 | ER-06 | **PASS** | 3/3 `exit=13`、无成品无 `.part` |
| G5-I3 | 录制中权限被收回 → 切备用路径 | ER-06 | **BLOCKED** | Windows DACL 对已打开句柄无效，留轮 23 |
| G5-I4 | 任意时刻强杀后可修复为可播文件，含音轨 | ER-02 | **PASS** | 3 场景 ×3，丢失 2.80~4.03s |
| G5-I5 | 编码器软编回退可用 | M8-08 | **PASS** | 3/3 `mf-h264-sw` |
| G5-I6 | 编码器全失败时给明确码且不留 0 字节 | M8-08 | **PASS** | 3/3 `exit=6`、无文件 |
| G5-I7 | GPU 复位（TDR）恢复 | ER-08 / M8-09 | **未覆盖** | 属 V1.5，不得以「未崩溃」代替 |
| G5-I8 | 恢复时强制后端不可用 → 漂移可见且不中断 | — | **未覆盖** | 并入 T-16b |
## 17. G5 兜底与降级判据（T-16b）

| # | 判据 | 锚点 | 状态 | 证据 |
| --- | --- | --- | --- | --- |
| G5-F1 | 写盘中途失败必须以非零码结束（不得静默成功） | M8-06 | **PASS** | `--fail-write-after 8` → exit 16，`write_errors>0` |
| G5-F2 | 失败后产物提交到兜底目录且可播 | M8-06 | **PASS** | 修复产物 6.41s，含音轨，解码零错误 |
| G5-F3 | 修复后时长 ≥ 已落盘 −1.5s | M8-06 | **PASS** | `src_pts=6408ms` → 6.41s ≥ 4.91s |
| G5-F4 | 兜底后原目录无成品、无 0 字节文件 | M8-06 | **PASS** | 目录条目数 0 |
| G5-F5 | 恢复时原后端不可用 → 真实降级且录制不中断 | C11 | **PASS** | `wgc→dxgi`，`backend_changed=1`，180 帧，成片可播 |
| G5-F6 | 恢复产物不覆盖已存在文件 | M8-02 | **PASS** | 二次修复产出 `_001`，首个产物完好 |
| G5-F7 | 修复后源 `.part` 消解，二次扫描不再提示 | M8-02 | **PASS** | 二次 `--scan` `scanned=0` |
| G5-F8 | 编码器全失败走真实双分支 | M8-08 | **PASS** | 错误同时含 hardware / software 两条注入 |

> 与轮 22 的差异：`--fail-write-after` 与 `--fail-encoder` 的注入点都下沉到了真实实现内部
> （`MkvWriter::WriteRaw` / `MfVideoEncoder::CreateTransform`），不再是调用方自造字符串。
## 18. 契约增补（轮 24，并入 §4 接口字段与退出码表）

### 18.1 `CaptureRequest` 新增字段（§4 附录）

| 字段 | 类型 | 语义 |
| --- | --- | --- |
| `excluded_backend` | `const char*` | 枚举期排除指定后端，用于验证真实降级（C11）。`nullptr` = 不排除 |

> 该字段只影响 `CaptureChain` 的候选顺序，不改变 `ICaptureSource` 契约。

### 18.2 进程退出码表

| 码 | 含义 | 产物状态 | 调用方处理 |
| --- | --- | --- | --- |
| 0 | 成功 | 成品已提交 | 正常 |
| 2 | 参数错误（未知选项 / 非法取值，如 `--format` 非法） | 无产物 | 打印 usage，不重试 |
| 6 | 编码器不可用（硬编+软编均失败） | 无产物 | 提示设备不支持 |
| 10 | 封装收尾失败 | `.part` 保留 | 建议走离线恢复 |
| 11 | 成品改名失败 | `.part` 保留 | 提示磁盘异常 |
| 12 | 磁盘空间不足，拒绝开录 | 无产物 | 提示清理空间 |
| 13 | 输出路径不可用 | 无产物 | 提示更换路径 |
| 14 | 倒计时被取消 | **无目录、无产物** | 不提示错误 |
| 15 | 运行期致命错误，数据完整或已保留 `.part` | 视情况 | 区分「成品可用」与「需恢复」 |
| 16 | 写失败，已提交兜底修复产物 | `fallback_path=` 指向成品 | 提示「已恢复」 |

> 16 只在**提供了 `--fallback-dir` 且修复成功**时出现；未提供兜底目录时统一返回 15。

### 18.3 G4 控制通道判据

> **轮 25 修订**：轮 24 曾把 §18.3 七条判据全标 PASS，但当时**没有实现 §7 的 Hello/HelloAck 握手**，
> 导致 `srctl` 不可用、`control` 通道对任意本地进程开放。该矛盾已修复：握手、令牌校验、初始快照
> 均已实现，判据以下表为准（旧的「连发 8 条 / 中途断开」两条基于自造客户端，已作废）。

| # | 判据 | 状态 | 证据 |
| --- | --- | --- | --- |
| G4-C1 | 未连接超时不阻塞录制 | **PASS** | `control=absent`，成片 3.999s |
| G4-C2 | `srctl` 端到端可用（握手 + 命令） | **PASS** | `handshake ok: pid=… protocol=1`，pause/resume/stop 均 exit 0 |
| G4-C3 | Pause/Resume 生效且状态正确 | **PASS** | ack 快照分别为 `Paused` / `Recording` |
| G4-C4 | Stop 提前收尾 | **PASS** | 20s 请求在 2.33s 结束，`Stopping,Idle`，成片可播 |
| G4-C5 | 暂停中 Stop | **PASS** | 序列 `Paused,Stopping,Idle`（不再多一次 Recording） |
| G4-C6 | 错误 token 被拒且录制不受影响 | **PASS** | `control=rejected`，成片仍 5.0s |
| G4-C7 | 半帧不消费（12 字节头 / 1 字节 payload 之间断开） | **PASS** | 恰解析 1 条 |
| G4-C8 | 录制期间不阻塞主循环 | **PASS** | 全部 `TryReceive`（Peek 判整帧） |
| G4-C9 | 手动暂停中控制通道断开 → ≤2s 自动恢复 | **PASS** | 暂停后客户端退出：`pause_auto_resumed=1`、`paused_ms=1505`、成片 5.999s、进程自行退出 |
| G4-C10 | 手动暂停超时主动收尾 | **PASS** | `--pause-timeout 3`：`pause_timeout_hit=1`、成片 1.999s（=已录内容）、`Paused,Stopping,Idle`；计时暂停 4s + timeout 2s 不被截断（F3，`g4_elapsed` B 段） |
| G4-C11 | 静默客户端不阻塞且被拒 | **PASS** | `control=rejected`；1s 握手超时在状态机中处理，wall 与无客户端差 0.00s（短录制 1.02s vs 1.02s） |
| G4-C12 | 重连补当前快照 | **PASS** | 暂停中重连：`state=Paused elapsed_ms=2211`（修复前为 `elapsed_ms=0`） |
| G4-C13 | 用例结束无 sr-recorder 残留 | **PASS** | `g4_channel` K 段 leftover=0 |

> **轮 26 修订（D-36 / N-2）**：手动暂停期间控制通道断开不再是永久等待——先给 1.5s
> 重连宽限（握手中的候选连接再追加 300ms，上限仍 < 2s），过期自动恢复并记
> `pause_auto_resumed=1`；若**手动**暂停累计超过 `--pause-timeout`（默认 900s），则主动收尾并记
> `pause_timeout_hit=1`。**计时暂停（`--pause-duration`）有明确终点，不受该超时截断**
> （F3：4s 计划暂停配 2s timeout 仍走完 4s）。两种情况下暂停时长均不计入成片，且
> 「非暂停时断开 → 录制继续」的既有语义不变。握手不在主循环里同步等待：静默客户端最多
> 占一个候选槽位，不拖慢帧循环。