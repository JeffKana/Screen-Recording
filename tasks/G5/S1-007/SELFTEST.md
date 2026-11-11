# SELFTEST: S1-007（轮 33）

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01` 第 105~106 行 M3-03/M3-04、第 172 行 M5-07；子句切分与覆盖表一致（M3-03 2 条、M3-04 2 条、M5-07 3 条） |
| 必读章节可定位 | 通过 | `docs/02` §5/§7、`docs/03` §6、`docs/05` §2/§6、`docs/06` §4.2/§6.1、`CONTRACT.md` §7/§18.3 均存在 |
| 上游交付物可获取 | 通过 | 轮 32 末 `build-msvc/bin/Release`（CBR 7/7、列表 24/24、诊断 7/7）；本轮从该基线增量编译 |
| 文件域无交集 | 通过 | 本卡写 `src/encode`、`src/recorder`、`src/app`、`verify/cases`、`verify/tools`；`tasks/G5/S6-002` 只写 `g5_lt01_real.ps1` + 自己的交付包，无重叠 |

## 1. 自测命令与输出

| 命令 | 结果 | 时间戳 |
| --- | --- | --- |
| `pwsh -File verify/cases/g5_format.ps1` | **14 PASS / 0 FAIL**（轮 35 裁定三条：运动源 avg 5,959 / 偏差 0.7%；静态 avg 1,556 只记录；峰值 6,632/1,561 < 9,000） | 2026-10-28 12:55 |
| `pwsh -File verify/cases/g5_fps.ps1` | **11 PASS / 0 FAIL**（六档持久化；UI 端到端 60.00 / 15.00fps） | 2026-10-27 09:33 |
| `pwsh -File verify/cases/g5_fileops.ps1` | **9 PASS / 0 FAIL**（重命名同步、回收站、批量确认、`.part` 保护） | 2026-10-27 09:36 |
| `pwsh -File verify/cases/g5_quality.ps1` | **5 PASS / 0 FAIL**（q70/q85 均 33.78dB / 0.9935） | 2026-10-27 10:18 |
| `pwsh -File verify/cases/g5_list.ps1` | **27 PASS / 0 FAIL**（含 3 条 size 断言） | 2026-10-27 10:25 |
| `pwsh -File verify/cases/g5_diag_real.ps1` | **9 PASS / 0 FAIL**（含 2 条源日志 intact） | 2026-10-27 10:25 |
| `python verify/tools/coverage_report.py --self-test` | `self_test ok`（三态 + 子句级优先） | 2026-10-27 10:25 |
| `python verify/tools/coverage_report.py --check` | `coverage table is up to date` | 2026-10-27 10:25 |
| `python verify/tools/contract_check.py` | **8 项检查, 8 PASS, 0 FAIL** | 2026-10-27 10:25 |
| `cmake --build build-msvc --config Release` | 成功，无告警 | 2026-10-27 18:44 |
| `cmake --build build-msvc --config Debug` | 成功，无告警 | 2026-10-27 18:44 |
| `cmake --build build-mingw --parallel 6` | 成功，无告警 | 2026-10-27 18:44 |
| `pwsh -File verify/cases/ui_smoke.ps1` | F2 改判后 **103/0**（`foreground_hwnd=2033852 app_hwnd=10882356`）；3 连跑由验收方执行 | 2026-10-28 13:0x |
| F2 红-绿（合成空标题 + 同 HWND） | 旧断言 True（假通过）/ 新断言 False（正确拒绝） | 2026-10-28 13:0x |

产物：`artifacts/g5_*_summary.json`、`artifacts/g5_list.txt`、`artifacts/g5_diag_real.txt`、
`artifacts/contract.txt`、`artifacts/coverage_selftest.txt`。

## 2. 红-绿对照

| 新断言 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| `m3-04-vbr-mode` | VBR 分支只设 `MF_MT_AVG_BITRATE` 并直接报 `rate_control=vbr`；首版接 ICodecAPI 时被 MFT 拒收：`rate_control=avg_bitrate error=CODECAPI VBR max bitrate/quality rejected` | `rate_control=vbr`、max=9000 回读一致、quality=80 |
| `m3-04-vbr-nominal` | 无均值目标，运动源下不可控 | avg 5,922 vs 6000（偏差 1.3%） |
| `m3-04-vbr-peak` | 无 max 约束 | 峰值 7,022 < 9,000 |
| `m3-04-mode-isolation` | 无 VBR 可切 | VBR 后 CBR spread 0.0%、filler>0、三份文件并存 |
| `fps-15-persist` … `fps-45-persist` | `--set-fps` 不存在（exit 1）；`[video]` 段不存在 | 六档写 INI + 重启 `app_start fps=N` 回读 |
| `m3-03-ui-60-cmd` | `RecorderController` 硬编码 `--fps 30` | `recorder_cmd fps=60` + 成片 `avg_frame_rate=60.00` |
| `rename-disk-sync` | 列表无右键菜单、无重命名实现 | `alpha.mkv → beta.mkv`，磁盘 + `list_entry` 同步 |
| `delete-recycle-bin` | 无删除实现 | `recycle=1`，回收站 `$I` 记录含原路径 |
| `batch-confirm-cancel` / `-yes` | 无批量与确认 | `confirm=0` 不删；`confirm=1` 全删 |
| 覆盖子句级（R1） | `- M3-04#1: 已实现` 被解析器忽略，M3-04#1 仍判未实现 | #1 已实现、#2 未实现；反向「子句未实现不被需求级拉绿」也通过 |

## 3. 回归

| 用例 | 结果 |
| --- | --- |
| `g5_diag` | 36 / 36 |
| `g5_inject` | 28 / 28（含 1 条既有 BLOCKED：DACL 对已打开句柄无效） |
| `g5_fallback` | 25 / 25 |
| `g4_gate` | 12 / 12（spawn→Recording 515ms、stop→playable 100ms、重复帧 1/1807） |
| `g4_control` | 32 / 32 |
| `ui_smoke` | **103 / 103 连跑 3 次**（轮 34 稳定化后）；修复前 92/9 → 98/3 → 101/0 的波动来自残留 `.part`、热键丢按、双屏物理光标漂移 |
| `g3_playback` | 11 / 11 |
| 契约 / 覆盖表 | C1~C8 8/8；`--check` up to date |

**回归依据**：改动集中在 `src/encode`（码控分支）、`src/recorder`（参数透传/日志）、
`src/app`（设置面板/列表文件操作）、`verify/tools`（覆盖模型）。因此抽样选
UI 全链路（`ui_smoke`/`g4_gate`）、控制闭环（`g4_control`）、录制故障路径
（`g5_inject`/`g5_fallback`）、编解码回放（`g3_playback`）与覆盖契约（C8）。

## 4. DoD 勾选

- [x] 任务卡每条需求都有对应证据（M3-04#2 / M3-03#1#2 / M5-07#1#2#3 各自一节）
- [x] 新增断言先红后绿，旧值已记录（§2）
- [x] MSVC Release + Debug 编译通过、零告警
- [x] 相关回归零失败（依据见 §3）
- [x] 无死代码、无调试残留、无注掉的块
- [x] 改动未超出文件域（新增 `src/app/FileOps.{h,cpp}` 已在卡内声明）
- [x] 接口有变更 → 已写入 HANDOFF（`RecorderController::Start` 签名、recorder CLI 新参数）
- [x] 文档已同步（`docs/07` 轮 33、`docs/09` 工具重生成、任务卡 `## 交付状态`）
- [x] `## 覆盖子句` 只列实际覆盖的子句
- [x] 已知限制已写明（VBR 静态欠码、画质档位无可测差异、8h 不在本卡）
