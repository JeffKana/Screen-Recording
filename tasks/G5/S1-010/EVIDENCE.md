# EVIDENCE: S1-010（M6-08#2 CPU 中转）

> 口径：执行方证据，非验收结论。数据 2026-11-09，`build-msvc/bin/Release`（SR_TEST_HOOKS=ON）。

## M6-08#2 如无法避免则走 CPU 中转仍能录制成功

- 现状普查：`src/compositor/Nv12Converter.cpp:492-519` 的 `Convert()` 在 GPU 路径失败时
  置 `force_cpu_=true`、`Resize()` 到 CPU staging，走 `ConvertOnCpu()`；
  能力已实现，只缺**稳定可重跑的触发入口**。
- 改动：`--force-cpu`（只在 `SR_TEST_HOOKS=ON` 构建）→ `Options.force_cpu` →
  `RecordingSession::Options::force_cpu` → `converter.ForceCpu()`（S2-003 提供的可选方法）。
  统计字段 `converter` 由 `UsingGpu()` 得出，未改判定逻辑。
- 命令：`pwsh -File verify/cases/g5_r42_closeout.ps1`（M6-08#2 段），
  独立复现：`sr-recorder.exe --record --seconds 3 --fps 30 --bitrate 6000 --force-cpu --out <f>`
- 输出：`tasks/G6/V1-003/artifacts/g5_r42_closeout.ps1.txt` — 3 条断言全 PASS：
  1. `m6-08-02-force-cpu-path`：`record_exit=0 converter=cpu frames_encoded=90`。
  2. `m6-08-02-cpu-decodable`：CPU 路径成片 duration=2.999s、video+audio 双轨、
     `ffmpeg -f null` 零错误。
  3. `m6-08-02-gpu-control`：不传开关时 `converter=gpu-videoprocessor`，
     证明开关确实切换了路径（不是永远 CPU）。
- 三构建：`tasks/G6/V1-003/artifacts/build-summary.txt` 全部 `exit=0 warnings=0 errors=0`；
  发布构建（hooks OFF）`build_release.ps1` 17/17，`--help` 不含 `--force-cpu`。
- **降级验证**：强制开关触发既有 CPU 中转路径，非真实混合显卡跨 GPU 故障；
  CPU 中转本身是既有产品能力。

## 已知限制
- M6-08#1「核显采集 + 独显编码不触发跨 GPU 拷贝」需特定硬件，本卡不覆盖。
- 执行方不自评为已验收。