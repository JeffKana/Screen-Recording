# HANDOFF: M3-03 / M3-04 / M5-06 挂账启动

## 现状

- M3-03：引擎支持任意 `--fps`（60/15 实测达标），**产品无入口**（设置面板/INI 都没有 fps）。
- M3-04：只有 `MF_MT_AVG_BITRATE`，实测 CBR spread 160%，**不达标**；无 VBR。
- M5-06：未开工。

## 下一步（按收益排序）

1. **M3-04#1 CBR**：这是三者中唯一「用户能感知」的缺陷（文件大小不可预测）。
   接 `ICodecAPI::SetValue(AVEncCommonRateControlMode, CBR)`；DirectShow 的 `strmif.h`
   已在项目里被刻意回避，若引入需评估构建影响，或换厂商 SDK。验收用
   `pwsh -File verify/cases/g5_format.ps1 -Seconds 40`，判据 spread ≤10%、均值偏差 ≤20%。
2. **M3-03 UI**：设置面板加 30/60 帧率，写 `[video] fps=`，`AppConfig` 读取并传给
   `RecordingSession::Options.fps`；回归 `ui_smoke` + `g5_format`（把 `--fps` 换成 INI 驱动）。
3. **M5-06**：依赖 M5-04 的索引；列表的「录制中实时更新」要复用 1Hz 快照，
   但**列表刷新不能加内存基线负担**（与 LT-01 冲突，注意不要引入每秒全量扫描）。

## 判据复用

`g5_format.ps1` 已经把「引擎是否达标」和「产品是否可交付」分开：脚本 PASS 只代表引擎，
覆盖表状态仍由 `TASK.md` 的 `## 交付状态` 决定。
