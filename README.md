# 录屏软件产品方案（Windows）

> 文档集状态：设计稿 · 平台：Windows 10 1903+ / Windows 11 · 交付边界：需求与方案，不含工程代码

## 一、产品定位

一款 **本地优先的 Windows 桌面录屏工具**。核心体验目标是三句话：

- **一键开录**：从唤起工具到开始录制，操作层级不超过两次点击一次热键。
- **录制稳定**：8 小时连续录制不崩、不掉盘、不涨内存，异常中断后文件仍可播放。
- **导出即用**：停止录制即得到一个可直接分享的 MP4，不需要额外转码工具。

与同类产品的差异化取舍：

| 取舍点 | 本方案选择 | 理由 |
| --- | --- | --- |
| 采集实现 | 仅用户态（WGC / DXGI / WASAPI），不装驱动 | 降低分发与签名成本，规避驱动崩溃导致的整机蓝屏风险 |
| 编码策略 | 硬编优先、软编确定性回退 | 硬编是低占用与不掉帧的前提，回退保证任何设备都能录 |
| 录制产物 | 录制中写分片 MKV，停止时 remux 为 faststart MP4 | 崩溃可恢复 与 即开即播 两个目标同时满足 |
| 编辑能力 | V1.0 不做，V1.5 起提供轻量时间轴 | MVP 阶段把预算全部投在录制可靠性上 |
| 云端能力 | 仅作为可选出口，不在主链路 | 本地优先，隐私敏感场景可完全离线 |

## 二、文档索引与阅读顺序

| 序号 | 文档 | 内容 | 主要读者 |
| --- | --- | --- | --- |
| 00 | [README.md](./README.md) | 产品定位、编号规则、术语表 | 全部 |
| 01 | [docs/01-功能需求清单.md](./docs/01-功能需求清单.md) | 8 大模块 144 条需求，含验收要点与版本标签 | 产品、测试、研发 |
| 02 | [docs/02-系统架构.md](./docs/02-系统架构.md) | 双进程模型、音视频管线、状态机、时间戳同步 | 研发 |
| 03 | [docs/03-技术选型.md](./docs/03-技术选型.md) | 六个维度的候选对比与推荐结论 | 研发、技术负责人 |
| 04 | [docs/04-里程碑与排期.md](./docs/04-里程碑与排期.md) | V1.0 / V1.5 / V2.0 范围映射与交付判据 | 项目管理 |
| 05 | [docs/05-非功能需求与验收.md](./docs/05-非功能需求与验收.md) | 性能基线、兼容性矩阵、异常恢复验收用例 | 测试、研发 |
| 06 | [docs/06-子智能体执行规程.md](./docs/06-子智能体执行规程.md) | 子智能体分工、门禁、输入输出规范、验收与评估闭环 | 项目管理、AI 协作编排 |
| 07 | [docs/07-执行台账.md](./docs/07-执行台账.md) | 每轮执行记录、缺陷明细、评估得分与修正方案 | 项目管理、研发 |
| 08 | [docs/08-阶段重排与任务清单.md](./docs/08-阶段重排与任务清单.md) | 实际进度快照、偏差归因、门定义调整、任务清单（优先级/依赖/状态） | 项目管理、研发 |
| 09 | [docs/09-V1.0覆盖表.md](./docs/09-V1.0覆盖表.md) | V1.0 66 条需求的「有卡/无卡 + 挂账理由」，由脚本生成 | 项目管理、研发 |

推荐阅读路径：

- **只想了解做什么** → 01 的模块总览表 + 各模块表格
- **要评估能不能做** → 02 数据流图 + 03 每个维度的「结论」行
- **要排期** → 04 三阶段范围表 + 05 兼容性矩阵的测试成本
- **要验收** → 01 每条需求的验收要点 + 05 的验收用例
- **要落地执行** → 06 的角色分工与门禁 + 任务卡模板

## 三、需求编号规则

```
M{模块号}-{两位序号}
例：M1-07 = 采集能力模块第 7 条
```

模块编号固定，不随版本调整：

| 编号 | 模块 | 对应原始需求维度 |
| --- | --- | --- |
| M1 | 采集能力（画面源与音频源） | 全屏、区域、窗口、摄像头画中画、系统声音与麦克风混音 |
| M2 | 录制控制 | 开始/暂停/恢复/停止、倒计时、快捷键 |
| M3 | 画质与参数 | 分辨率、帧率、码率、格式 |
| M4 | 编辑与导出 | 裁剪、拼接、标注、字幕、导出 |
| M5 | 文件管理 | 保存路径、命名、格式转换、云端上传 |
| M6 | 性能与兼容性 | 低占用、多平台支持、长时间录制稳定性 |
| M7 | 辅助功能 | 定时录制、鼠标高亮、水印、隐私遮挡 |
| M8 | 异常处理 | 中断恢复、存储不足提示 |

## 四、版本标签定义

| 标签 | 含义 | 判据 |
| --- | --- | --- |
| **V1.0**（MVP） | 可对外发布的第一个版本 | 缺了它，「录一个能用的 MP4」这件事就不成立 |
| **V1.5**（增强） | 补齐专业场景与体验短板 | 缺了它不影响录制成功，但会明显流失进阶用户 |
| **V2.0**（进阶） | 编辑、云端、HDR 等能力扩展 | 属于第二增长曲线，可在 V1.5 上线后按数据排优先级 |

补充约定：

1. **每条需求有且只有一个版本标签**，不存在「跨版本」的条目。若一条需求横跨两个版本，拆成两条。
2. **每条需求必须有可测的验收要点**。验收要点写「观察什么现象 / 达到什么数值 / 出现什么提示」，不写「体验良好」「性能优秀」这类不可判定的描述。
3. **V1.0 的冻结原则**：V1.0 开发期内新增需求一律进 V1.5 候选池，不回填 MVP。

## 五、术语表

| 术语 | 说明 |
| --- | --- |
| **WGC** | Windows.Graphics.Capture，Win10 1803+ 提供的窗口/显示器采集 API，D3D11 纹理直出，支持被遮挡窗口采集 |
| **DXGI DD** | DXGI Desktop Duplication，桌面复制 API，独占全屏/游戏场景延迟低，但只支持整屏 |
| **GDI BitBlt** | 传统屏幕位图抓取，CPU 拷贝，作为老旧设备与 WGC 失败场景的兜底路径 |
| **WASAPI** | Windows Audio Session API。**Loopback** 指抓取某个输出设备的混音（系统声音） |
| **PiP** | Picture-in-Picture，摄像头画中画，即摄像头画面叠加在屏幕画面之上的小窗 |
| **remux** | 重新封装：只搬容器不重编码，把 MKV 分片快速转成 MP4，几乎无损且快于实时 |
| **faststart** | MP4 的 moov 原子前置布局，使文件无需下载完整即可边下边播 |
| **分片（segment）** | 录制过程中按时间或大小切分的独立文件，用于崩溃恢复与突破单文件大小限制 |
| **PTS / DTS** | 显示时间戳 / 解码时间戳，音视频同步的基准 |
| **QPC** | QueryPerformanceCounter，Windows 高精度单调时钟，本方案的时间基准 |
| **TDR** | Timeout Detection and Recovery，显卡驱动超时复位，会导致 D3D 设备丢失 |
| **DRM 内容** | 受保护内容（如 Netflix/部分播放器），采集结果为黑屏，属系统级限制 |
| **混合 DPI** | 多显示器下各屏缩放比例不同（如 100% + 150%），坐标换算易错的高发场景 |
| **背压** | 下游处理不过来时对上游施加的流量限制，本方案用有界队列 + 丢帧实现 |

## 六、工程产物（G1 已落地）

文档之外，工作区已包含 G1 基座的实现骨架：

```
CONTRACT.md            接口与构建契约（改动需走变更单）
src/core/              类型、8 个抽象接口、错误总线、录制状态机（零平台依赖）
src/platform/          ComPtr + win/QpcClock（唯一的平台时间源）
src/ipc/               命名管道 + 12 字节头分帧协议
src/capture/           DXGI 全屏 / 区域裁剪 / PrintWindow 窗口兜底 + 工厂降级
src/audio/             WASAPI 回环与麦克风 / float32 混音器 / 音频时间轴
src/recorder/          Recorder 进程骨架（握手、状态机驱动、单实例）
src/app/               Win32 UI（托盘/热键/子进程控制，仅依赖 sr_ipc + sr_core）
src/tools/srctl/       控制通道诊断探针
src/tools/srprobe/     采集与音频实测探针（含窗口/混音自检）
tasks/G1 tasks/G2      任务卡、证据、交接说明
verify/                契约核验工具与验收用例
build-mingw/           MinGW 验证构建产物（非交付物）
```

常用命令：

```powershell
# 契约级静态核验（8 项：C7 覆盖非空 + C8 未切片逐条挂账）
python verify/tools/contract_check.py --verbose

# V1.0 覆盖表（66 行，脚本生成；只手工填最后一列「挂账理由」）
python verify/tools/coverage_report.py --write
python verify/tools/coverage_report.py --check

# G1：状态机全转移 + 握手 + 单实例（40 条断言）
pwsh -NoProfile -File verify/cases/g1_walk.ps1 -BuildDir build-mingw

# G2：采集三路径 + 音频时间轴 + 混音自检
pwsh -NoProfile -File verify/cases/g2_walk.ps1 -BuildDir build-mingw

# G4：控制闭环（暂停/倒计时/取消/自动停止）
pwsh -NoProfile -File verify/cases/g4_control.ps1 -BuildDir build-msvc -Config Release

# G4：控制通道（握手/暂停断线自动恢复/暂停超时/静默客户端/重连快照，52 条）
pwsh -NoProfile -File verify/cases/g4_channel.ps1 -BuildDir build-msvc -Config Release

# G4：媒体时间（暂停冻结 + 与成片差 ≤0.5s + 计时暂停不受 timeout 截断，13 条）
pwsh -NoProfile -File verify/cases/g4_elapsed.ps1 -BuildDir build-msvc -Config Release

# G4/UI：骨架/托盘/热键/自检/区域/剩余时长/保存路径/模板/悬浮条（101 条，热键用 keybd_event）
pwsh -NoProfile -File verify/cases/ui_smoke.ps1 -BuildDir build-msvc -Config Release

# G4 门：60s 全链路（停止到可播 ≤3s + 重复帧 ≤1%，带运动源）
pwsh -NoProfile -File verify/cases/g4_gate.ps1 -BuildDir build-msvc -Seconds 60

# G5：故障注入矩阵（磁盘/路径/强杀/编码器，各 3 次）
pwsh -NoProfile -File verify/cases/g5_inject.ps1 -BuildDir build-msvc -Config Release

# G5：写失败兜底 + 后端真实降级 + 恢复命名
pwsh -NoProfile -File verify/cases/g5_fallback.ps1 -BuildDir build-msvc -Config Release

# T-17：可观测性（1Hz 三项指标 + 丢帧注入 + 诊断包四类产物 + 日志分级 + 三态覆盖，36 条）
pwsh -NoProfile -File verify/cases/g5_diag.ps1 -BuildDir build-msvc -Config Release

# 轮 31：U1 定量（ui_smoke 连跑 10 次，失败即停并取证，约 11 分钟）
pwsh -NoProfile -File verify/cases/soak_ui_smoke.ps1 -Runs 10

# 轮 31：LT-01 代理（4 段 × 30min × 3 轮 ≈ 6h；冒烟用 -Seconds 20）
pwsh -NoProfile -File verify/cases/g5_lt01.ps1 -Rounds 3 -Segments 4 -Seconds 1800

# 轮 31：LT-02 代理（30min 静音段 + 恢复段，约 32 分钟）
pwsh -NoProfile -File verify/cases/g5_lt02.ps1 -MuteSeconds 1800 -RecoverySeconds 30

# 轮 31：M3-03/M3-04 引擎级（60/15fps + CBR/VBR 逐秒码率，VBR 会起全屏运动源）
pwsh -NoProfile -File verify/cases/g5_format.ps1 -Seconds 10

# 轮 33：帧率产品入口（六档持久化 + UI 端到端 15/60fps）
pwsh -NoProfile -File verify/cases/g5_fps.ps1

# 轮 33：文件操作（重命名同步 / 回收站删除 / 批量确认 / .part 保护）
pwsh -NoProfile -File verify/cases/g5_fileops.ps1

# 轮 33：画质 70/85 对照（PSNR/SSIM 对源图）
pwsh -NoProfile -File verify/cases/g5_quality.ps1

# 轮 33/34：LT-01 真·单文件 8h（不挂 UI；约 8h + 全解 5min，先留 ≥35GB）
# 运行中会写 verify/artifacts/.lt01-running.json；看到它就不要跑任何用例。
# 长跑镜像名是 sr-recorder-lt01，普通用例按 sr-recorder 清理不会误杀（D-60）。
pwsh -NoProfile -File verify/cases/g5_lt01_real.ps1 -Seconds 28800 -SampleSeconds 60

# 诊断包手工导出（故障现场用；只写 version/gpu/log/config 四类文本）
build-msvc\bin\Release\sr-ui.exe --export-diag %TEMP%\sr-diag
build-msvc\bin\Release\sr-recorder.exe --export-diag %TEMP%\sr-diag --log-level info

# G3：真实解码回放（需要 ffmpeg/ffprobe）
pwsh -NoProfile -File verify/cases/g3_playback.ps1 -BuildDir build-mingw -Seconds 6

# G3：容器结构独立核验（纯 Python，无外部依赖）
python verify/tools/mkv_probe.py build-mingw\t8.mkv
```

阶段状态见 [07-执行台账](./docs/07-执行台账.md)：

- **G1 达标（S=95.0）**：E4 已关闭（MSVC Release/Debug 零告警，产物解码验证通过）。
- **G2 勉强达标（S=80.4）**：采集三路径与音频链路实测通过；挂账混合 DPI、设备切换、全屏吞吐复测。
- **G3 勉强达标（S=79.1）**：可产出并**真实解码回放**的 MKV（180 帧全解零错误、画面非空）；挂账音频轨、三厂商硬编。
- **G4 达标（轮 29）**：轮 27 Win32 骨架/托盘/热键，轮 28 剩余时长/路径/模板/悬浮条，
  轮 29 自检/区域框选/微调；`ui_smoke` 101/101；`g4_gate` 12/12（60s 素材 stop→playable 85ms、
  重复帧 0/1806=0%）。
- **T-17 可观测（轮 30）**：三项指标 1Hz 由 recorder 计数下发（快照扩展块），丢帧可注入；
  诊断包四类文本产物（<5MB / ≤2s / 用户名脱敏 / 无画面音频）；日志 `debug|info|warn`；
  覆盖表补「有卡未实现」第三态，C8 8/8。V1.0 发布判据（66 条全过、LT/ER 各 3 次）仍未满足，见 08 §23.5。
- **轮 40 · UAC 提权提示（M1-27）**：非管理员运行时，界面**常驻提示**「UAC 提权窗口等高权限窗口可能黑屏或无画面」，
  并提供**「以管理员身份重启」入口**（主窗口按钮 + 托盘菜单，走 `ShellExecute(runas)`）；
  开录前自检也列出该项（**非阻断**，录制照常开始）。
  「成片该窗口区域一定黑屏」这一条在本机**不可复现**（UAC 不落安全桌面），
  缺什么、怎么人工补验见 `tasks/G5/S5-003/EVIDENCE.md`（该子句仍挂「有卡未实现」）。

## 七、文档约定

1. **性能指标**一律写成「基线 + 测量方法 + 达成条件」，参考数值标注为**设计目标**而非实测承诺。
2. **已知限制**（如 DRM 黑屏、UAC 提权窗口）显式记录在 01 对应需求与 03 风险表，不做隐藏。
3. 文档中的接口、模块、字段命名以**职责描述**为主，不绑定具体实现签名，避免文档随代码演化迅速失效。
4. 所有 mermaid 图在 GitHub / VS Code 预览下可渲染；若渲染失败，图下均附文字说明。
