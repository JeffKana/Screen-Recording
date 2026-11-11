# EVIDENCE: V1-004（轮 41 · 无卡收口 5 条）

> 口径声明：**清账型交付**。5 条子句的产品能力已在，本卡只补 TASK 追溯与专属行为断言。
> 降级/代理验证逐条显式标注，不用文案冒充实测；口径冲突另提 `docs/15` 裁定记录（轮 42 已裁定）。
> 数据来源：2026-11-09，`build-msvc/bin/Release`（SR_TEST_HOOKS=ON），Win11 build 26200 / 双屏 1920×1080。
> 完整输出：`tasks/G6/V1-002/artifacts/g5_closeout.txt`（UTF8，`CLOSEOUT_EXIT=0`）。
> 可重跑总命令：`pwsh -File verify/cases/g5_closeout.ps1`

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | 5 条子句的验收要点原文逐条抄自 `docs/01`：M8-01:251、M6-10:205、M6-11:206、M3-15:117、M6-04:199；切分结果与 `docs/09` 子句表一致 |
| 必读章节可定位 | 通过 | `docs/03 §编码选型`（x264 GPL 决策）、`docs/05 §1/§2.1/§7.2`、`docs/06 §4.2/§4.3`、`docs/12`（裁定先例）、`verify/lib/Cleanup.ps1` 均在位 |
| 上游交付物可获取 | 通过 | `src/resilience/MkvRecovery.cpp`、`packaging/install.ps1`、`src/app/main.cpp:664`（`FormatMetrics`）、`src/recorder/RecordingSession.cpp:1015`（丢帧注入）、`src/encode/EncoderList.cpp`；`g3_recovery` 9/9、`g5_inject` 28/28、`g5_encoder_list` 17/17 既有证据 |
| 文件域无交集 | 通过 | 本卡 `verify/cases/g5_closeout.ps1` + `docs/15` + `tasks/G5/V1-004/*`；与 V1-002（verify/tools、docs/07/09/14/16）、S1-009（src/recorder）目录互斥 |

## 1. M8-01#1 中断恢复 ≤10s

- 验收要点原文：录制 5 分钟后强制结束进程，重启后可恢复到崩溃前 ≤10s 的内容，文件可正常播放。
- 验证方式：`Start-Process` 录 60s → 12s 时 `Stop-Process -Force` → `--scan` → `--repair` →
  ffprobe 读 duration / ffmpeg 全片解码。
- 命令：`pwsh -File verify/cases/g5_closeout.ps1`（本段断言 `m8-01-01-*`）
- 输出（`artifacts/g5_closeout.txt`）：

```
PASS m8-01-01-part-left — 强杀后遗留 .part=…\sr-closeout\m8-01.mkv.part
PASS m8-01-01-scan-repairable — --scan 报告存在可修复分片
PASS m8-01-01-repair-ok — repair=ok，成品 …\m8-01.mkv.recovered.mkv
PASS m8-01-01-loss-within-10s — killed_at=12s recovered=9.450s loss=2.550s（判据 ≤10s；降级验证：12s 短录代理 5min 长录）
PASS m8-01-01-decodable — ffmpeg 解码零错误，video=True audio=True
```

- 结论：**通过（降级验证）**。12s 强杀代理需求原文的 5 分钟长录，未冒充全量实测；
  丢失 2.550s ≤10s，成片 video+audio 双轨、解码零错误。既有 `g3_recovery.ps1` 9/9
  （`loss-within-10s`）为旁证。

## 2. M6-10#1 安装包无驱动组件

- 验收要点原文：安装包无驱动组件。
- 验证方式：zip 条目清单 + `install.ps1` 文本 + `src/` 源码三方静态断言。
- 命令：`pwsh -File verify/cases/g5_closeout.ps1`（本段断言 `m6-10-01-*`）；
  `Expand-Archive dist/ScreenRecorder-0.1.0-win-x64.zip` 后核对 6 条目。
- 输出：

```
PASS m6-10-01-zip-no-driver — 包内 6 条目，驱动类条目=0；清单：install.ps1, README.txt, uninstall.ps1, VERSION.txt, bin/sr-recorder.exe, bin/sr-ui.exe
PASS m6-10-01-install-userland-only — install.ps1 驱动/服务安装调用命中数=0（期望 0）；命中：无
PASS m6-10-01-source-no-driver — src/ 驱动二进制=0，驱动装载 API 命中文件=0（期望均为 0）
```

- 结论：**通过**。检查项含 `.sys/.inf/.cat/.vxd/.drv/.dll`、`drivers/` 目录、
  `pnputil`/`sc.exe`/`New-Service`/`NtLoadDriver`/`SetupCopyOEMInf`/`CurrentControlSet\Services`/`HKLM:\SYSTEM`。
  既有 `tasks/G6/V1-001/EVIDENCE.md` 的包结构树为旁证。HVCI（#2）不在本卡范围，仍无卡。

## 3. M6-11#1 面板数值随录制状态更新

- 验收要点原文：面板数值随录制状态更新。
- 验证方式：recorder `--log-file` 的 1Hz `metrics` 行 + UI 状态栏（控件 ID 200）就绪/录制两态对照，
  间隔 1.2s 两次采样验证刷新。
- 命令：`pwsh -File verify/cases/g5_closeout.ps1`（本段断言 `m6-11-01-*`）
- 输出：

```
PASS m6-11-01-metrics-1hz — 录制日志中 1Hz metrics 行=4（含非零采集/编码 fps 的行=4）；样例：metrics capture_fps=45.3 encode_fps=30.5 dropped=0 captured=46 window_ms=1015
PASS m6-11-01-ui-status-refresh — hasMetrics=True idleChanged=True；录制态首行：录制中  00:01  ·  剩余可录 05:21:00  ·  全屏
PASS m6-11-01-ui-timer-updates — 1.2s 间隔两次采样不同；t1=录制中 00:01 … t2=录制中 00:02 …
```

- 结论：**通过（已知限制）**。产品呈现的是三项指标（采集/编码 fps、丢帧），
  需求标题另列的码率/队列深度/写盘速度/编码器状态**未呈现**；#2「背压可见标识」仍无卡。
  本卡只按验收要点原文「面板数值随录制状态更新」判定，不写成「五项指标齐全」。

## 4. M3-15#1 定点丢帧策略

- 验收要点原文：人为制造 GPU 压力导致实际 45fps（配置 60fps），成片时长仍准确、音画同步、无加速播放现象。
- 验证方式：`--record --seconds 6 --fps 60 --inject-drop-every 4`（每 4 帧丢 1 → 实际约 45fps）
  → 断言丢帧计数、成片 duration、avg_frame_rate、音轨、ffmpeg 解码。
- 命令：`pwsh -File verify/cases/g5_closeout.ps1`（本段断言 `m3-15-01-*`）
- 输出：

```
PASS m3-15-01-drop-injected — record_exit=0 frames_dropped=89 frames_encoded=271（60fps×6s=360 帧；降级验证：注入代理 GPU 压力）
PASS m3-15-01-duration-accurate — 成片 duration=5.983s vs 请求 6.000s（偏差 0.017s ≤0.5s）；wall=6.05s
PASS m3-15-01-no-speedup — 时间轴仍按配置 60fps 铺排（avg_frame_rate=60/1），丢帧未压缩时间轴
PASS m3-15-01-av-sync — 视频轨 + 音轨俱在且 ffmpeg 全片解码零错误（音画同步代理判据，非逐帧 A/V 偏差实测）
```

- 结论：**通过（代理验证）**。注入丢帧 ≠ 真实 GPU 压力；音画同步是「双轨 + 全片解码零错误」
  的代理判据，不是逐帧 A/V 偏差实测。两者都在断言文案里写明。

## 5. M6-04#2 均失败落到软编且录制成功

- 验收要点原文：均失败时落到 **x264** 且录制成功。
- **口径冲突**：`docs/03-技术选型.md:102-109` 明确「Media Foundation H.264 软件 MFT … 采用为软编回退首选」、
  「为了规避 GPL，主动放弃了画质最优的 x264」；实际实现 `encoder=mf-h264-sw`。
  `docs/01` 的 M3-05#1 把 x264 写作「**如** … x264」的举例；M3-06#1 / M8-08#1 已按「自动切软编」判已实现。
  本卡**不自行改判据、不写成「x264 已实现」**，冲突全文见 `docs/15-轮42-M6-04-2-x264口径裁定记录.md`。
- 验证方式：`--list-encoders` 三厂商不可用原因 + 软编回退录制成功 + 全失败双分支。
- 命令：`pwsh -File verify/cases/g5_closeout.ps1`（本段断言 `m6-04-02-*`）
- 输出：

```
PASS m6-04-02-hw-unavailable-with-reason — 三厂商硬编 id 命中=3，逐条标注 available=0 + 非空 reason 的规范行=3；nvenc-h264, qsv-h264, amf-h264
PASS m6-04-02-fallback-recording-success — 硬编全不可用 → encoder=mf-h264-sw record_exit=0 成片可解码=True
PASS m6-04-02-allfail-dual-branch — 连软编也失败时才报错：record_exit=6 且 hardware/software 双分支可见
```

- 结论：**按软编等价口径通过，待裁定**。功能目标（硬编全不可用 → 落到一条永远可用的软编链路 →
  录制成功）有真实行为断言；若验收方裁定「x264 为字面硬要求」，本条应判 PARTIAL 并回退为
  「有卡未实现」（覆盖 99 → 98）。执行方不自评为验收通过。

## 6. 覆盖表结果

- 命令：`python verify/tools/coverage_report.py --write`，再 `--check` → `coverage table is up to date`
- 输出（`docs/09:25`）：`已实现 99，有卡未实现 1，无卡 16，未实现已挂账 17`；
  合计行 `99 | 1 | 16 | 17 | 0`（缺理由 0）。**判据 ≥99 / ≤16 达成**。
- 五条归属均为 `tasks/G5/V1-004/TASK.md`，状态 `已实现`。

## 7. 首轮取数缺陷（非放宽判据）

首轮 `15 PASS / 1 FAIL / 1 SKIP`，两处都是**用例取数**，不是产品缺陷，也不是判据问题：

| 现象 | 根因 | 修复 | 断言变化 |
| --- | --- | --- | --- |
| `m6-04-02-hw-unavailable-with-reason` FAIL（原因行=2，期望 ≥3） | 把 `--list-encoders` 的 `encoders=[...]` 汇总串（三个 `state=unavailable` 挤在一行）当逐条行计数 | 改为只数 `^encoder=id=… available=0 reason=<非空>` 规范单行 | **收紧**：`Count -ge 3` → `Count -eq 3` |
| `m6-11-01-ui-status-refresh` SKIP（`app_ready=False`） | UI 进程持有日志写句柄，`[System.IO.File]::ReadAllText` 抛共享冲突返回空串 | 改用 `verify/lib/Cleanup.ps1` 的 `Read-TextSafe`（FileShare.ReadWrite） | **不变**，只是取数修复；SKIP → PASS |

修复后重跑 `18 PASS / 0 FAIL / 0 SKIP`。

## 8. 已知限制

1. M8-01#1：12s 强杀代理 5min 长录（降级验证）。
2. M3-15#1：注入丢帧代理 GPU 压力；音画同步为双轨+解码代理判据（非逐帧偏差）。
3. M6-11#1：仅三项指标；需求标题另四项未呈现；#2 背压标识仍无卡。
4. M6-04#2：x264 术语冲突，待 `docs/15` 裁定；验收方可判 PARTIAL。
5. 本卡全部结论待验收方独立重跑，执行方不自评为已验收。
