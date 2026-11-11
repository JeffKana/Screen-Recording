# 独立重跑清单

> 验收方必须自己跑。执行报告只当索引，不当证据。
> 用例脚本在 `<项目根>/verify/cases/`，工具在 `<项目根>/verify/tools/`。

## 0. 通用前置

| 项 | 要求 |
| --- | --- |
| 构建目录 | `-BuildDir build-msvc`（主链）或 `build-mingw`；**先确认当前主链是哪一个**，并在报告里写明 |
| 产物自清理 | 用例必须清理本轮目标产物再跑，避免读到共享目录旧文件掩盖回归 |
| 失败取证 | 断言失败自动落 `verify/artifacts/fail-<时间戳>-<tag>/context.txt`（进程 / 命令行 / 日志尾部 / `.part`） |
| 时间戳 | 每条命令记录开始时间，写进验收报告 |
| 残留进程 | 每轮结束后确认 `sr-recorder` / `sr-ui` / `srctl` 残留为 0 |
| 磁盘 | LT 类用例每轮产物 ≈5.4GB，开跑前清理旧段，别把盘写满——那测的就不是长录制 |

## 1. 用例矩阵

| 用例 | 判据来源 | 本项目期望值 |
| --- | --- | --- |
| `verify/cases/g1_walk.ps1` | CONTRACT §8 基座退出判据 | 40 / 40 |
| `verify/cases/g2_walk.ps1` | CONTRACT §9 采集与音频链路 | 18 / 18；环境上限项 SKIP 需说明原因，不计 FAIL |
| `verify/cases/g3_output.ps1` | CONTRACT §14 输出路径 | 15 / 15 |
| `verify/cases/g3_playback.ps1` | CONTRACT §10 编码封装 | 11 / 11（真实解码回放，180 帧零错误、画面非空） |
| `verify/cases/g3_recovery.ps1` | CONTRACT §12 崩溃恢复 | 9 / 9 |
| `verify/cases/g3_storage.ps1` | CONTRACT §13 存储与异常 | 11 / 11 |
| `verify/cases/g4_channel.ps1` | CONTRACT §18.3 控制通道 | 52 / 52 |
| `verify/cases/g4_control.ps1` | CONTRACT §15 控制闭环 | 32 / 32 |
| `verify/cases/g4_elapsed.ps1` | M2-09 计时口径 | 13 / 13 |
| `verify/cases/g4_gate.ps1` | CONTRACT §15 / G4 门 | 12 / 12；stop→playable ≤3s（60s 素材） |
| `verify/cases/g5_diag.ps1` | 诊断包判据 | 36 / 36；真实 30min 日志导出：首行时间戳差 ≤11min 且 ≤400KB |
| `verify/cases/g5_fallback.ps1` | CONTRACT §17 兜底与降级 | 25 / 25 |
| `verify/cases/g5_format.ps1` | 05 编码判据 | **6 / 6**：M3-03 帧率达标；M3-04 CBR spread ≤10% 且 nominal ±20% |
| `verify/cases/g5_inject.ps1` | CONTRACT §16 故障注入 | 28 / 28 |
| `verify/cases/g5_lt01.ps1` | 05 长录制判据 | 12 段（或经批准的 6 段）；段间 mem ≤+5%、handles ≤10%、每段 30min±1s、丢帧 0、可播 |
| `verify/cases/g5_lt02.ps1` | 05 音频判据 | 8 / 8；静音段 ≤-50dB、恢复段有声不削波、起播无满幅尖峰 |
| `verify/cases/ui_smoke.ps1` | UI 判据 | 101 / 101 |
| `verify/cases/soak_ui_smoke.ps1` | 不稳定性定量 | `-Runs 10` 连跑全绿；**保留「不稳定（低频、未复现）」标签**，不因连绿撤销 |
| `verify/cases/stimulus.ps1` | 辅助 | 按用例需要 |

## 2. 工具与契约检查

| 命令 | 期望 |
| --- | --- |
| `verify/tools/contract_check.py` | 8 / 8（C1 任务卡齐、C7 覆盖非空、C8 未切片逐条挂账 等） |
| `verify/tools/coverage_report.py --check` | up to date；覆盖模型收紧后 C8 仍 8 / 8 |
| `verify/tools/mkv_probe.py` | MKV 结构自校验，容器可被 libavformat 探测 |
| `verify/tools/mkv_dump.py` | 需要逐元素核对时使用 |
| 双构建：MSVC + MinGW，Release + Debug | **零告警**（06 §6.2：零警告升级为错误项） |

## 3. 契约级硬门（06 §6.2，与需求判定并行）

| 检查项 | 判据 | 不合格后果 |
| --- | --- | --- |
| 构建 | Release + Debug 均编译通过，零警告 | 门不通过 |
| 接口稳定性 | 与本阶段 `CONTRACT.md` 一致，无未登记变更 | 门不通过 |
| 依赖白名单 | `vcpkg.json` 无新增；FFmpeg 保持 `--disable-gpl` | 直接不达标 |
| 许可合规 | 未引入 GPL 组件 | 直接不达标 |
| 日志规范 | 关键路径有分级日志，无敏感内容（画面/音频） | 扣完整性分 |
| 文档同步 | 接口变更已更新到 `CONTRACT.md` | 扣一致性分 |

## 4. 复跑策略

| 情形 | 策略 |
| --- | --- |
| 需求级断言失败 | 单条重跑最多 1 次；仍失败即 FAIL，不做无上限重试 |
| 环境类失败（DWM 节流、虚拟显示、会话空闲） | 判 SKIP/BLOCKED 并记录环境证据，不算产品缺陷 |
| 用例自身缺陷（取值错、读旧产物） | 记为**用例缺陷**并单独开单，不算产品 FAIL；但必须修正后重跑该条 |
| 全量回归 | 改动涉及共享层（`src/core/`、`src/ipc/`）→ 全量；否则按文件域抽样并说明依据 |
