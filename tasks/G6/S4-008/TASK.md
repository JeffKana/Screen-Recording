# TASK: S4-008 轮 47 · M3-05 列表与开录结果一致（真激活探测 + 强制不回退）

阶段：G6｜角色：S4（产品实现卡，随主卡 `V1-020` 交付）｜轮次：47
来源：`.codebuddy/plans/轮47指令.md` §P0-1 / §通过判据 #1 / §注意事项 #1~#3、`验收报告-轮46.md` §2 F1

> 规划侧只下发了轮 47 指令（`.codebuddy/plans/轮47指令.md`），本卡是该指令在本文件域内的
> 派生执行卡；开工四项自查见 `tasks/G6/V1-020/SELFTEST.md §0`。本轮无并行任务卡，
> `S4-008` 与 `V1-020` 文件域已划开、无交集。

## 需求范围

| # | 需求 | 验收要点原文（`docs/01`） | 判据（照抄轮 47 指令） | 证据形式 |
| --- | --- | --- | --- | --- |
| P0-1 #1 | M3-05#1 | 「界面显示实际可用列表（如 NVENC / QSV / AMF / x264）」 | `--list-encoders` 里每个 `available=1` 的项，`--encoder <id>` **必须真的用上它** | 正向用例逐项跑 `--encoder <id>`，比对 `encoder=<id>` |
| P0-1 #2 | M3-05#2 / M3-06#2 | 「不可用项不出现或标注不可用原因」/「强制指定硬编但不可用时开录前即报错」 | 每个 `available=0` 的项，`--encoder <id>` 必须 `exit 6` 且原因非空 | 反向用例逐项跑，取 `record_exit` 与日志原因 |
| P0-1 #3 | M3-05#1 | 同上 | 人为让 `mf-h264-hw` 激活失败（`--fail-encoder hardware`），列表里它必须变成 `unavailable` | 注入前后各跑一次 `--list-encoders`，比对状态与原因 |

**修法（A + B 都做，轮 47 指令 §注意事项 #1「建议都做」）**

- **A 列表诚实**：`ListEncoders()` 的 MF 项不再用「`ActivateObject` 成功」冒充可用，改为
  走**产品自己那条编码路径**真激活：创建 MFT → 协商 NV12/H.264 输入输出类型 → 下发码控 →
  试提交 NV12 帧并取回 H.264 包；任一阶段失败 = `unavailable` + `reason`。
  这样「列表说可用」与「产品打不开」不可能分叉（同一份实现，不是两边各写一遍）。
- **B 选择诚实**：`requested_id_` 非空时禁止硬编→软编回退（`Negotiate` / `ApplyRateControl`
  两处），失败走既有 `exit 6` + 原因；自动选择（`requested_id_` 为空）的 M3-06 回退语义不变。
- **探测口径说明**：录制侧编码器只吃 CPU NV12（`MfVideoEncoder::SubmitBytes`，
  `Submit(VideoFrame)` 恒 false），本产品**不在编码器路径创建或持有 D3D device / video processor**。
  因此探测器同样不创建 D3D 资源，D-25 旧账（转换器绑在已释放设备上）在本卡没有泄漏面；
  所有 COM/MF 资源用 `ComPtr` + 成对 `MFStartup/MFShutdown`、`CoInitializeEx/CoUninitialize` 管理，
  失败分支也释放，保证可重试。该设计差异如实写进 `HANDOFF.md`，不冒充「D3D 试一帧」。

## 必读章节

- `docs/01-功能需求清单.md` M3-05 / M3-06 原文、`## 变更记录`
- `docs/02-系统架构.md` §7 模块职责与接口契约
- `docs/05-非功能需求与验收.md` §5 AC-08、§7.2 版本准入门槛
- `docs/06-子智能体执行规程.md` §2 文件域、§4.2 开工四项自查
- `docs/20-研发协作SOP.md` §0 门禁
- `CONTRACT.md` §2 文件域、§6 错误码表、§18.2 进程退出码表（码 6）
- `docs/14-轮41-兼容性矩阵草案.md` §3 硬编抽测格（轮 46 记 PARTIAL）
- `docs/19-轮44-收口可行性与判据矛盾核查.md` §0.B
- `.codebuddy/plans/验收报告-轮46.md` §2 / §4 F1；`.codebuddy/plans/轮47指令.md` 全文

## 文件域（白名单）

- `src/encode/EncoderList.h`、`src/encode/EncoderList.cpp`
- `src/encode/MfVideoEncoder.h`、`src/encode/MfVideoEncoder.cpp`
- `src/recorder/main.cpp`、`src/recorder/RecordingSession.h`、`src/recorder/RecordingSession.cpp`
- `tasks/G6/S4-008/*`（本卡四件套与本地证据）

> 不在白名单：`src/core/`（含 `VideoEncoderConfig`，本卡不改跨模块结构）、`src/capture/`、
> `src/compositor/`、`src/app/`、`CMakeLists.txt`、`vcpkg.json`、`verify/`（用例由 `V1-020` 负责）、
> `CONTRACT.md`（无 IPC / 状态机 / 退出码变更）、`docs/`（由 `V1-020` 负责）。

## 上游交付

- `main` @ `560087a`（Merge PR #6 `acceptance/r46`）；分支 `round/47` 从该提交拉出
- `verify/cases/g5_encoder_list.ps1` 17 条断言、`verify/cases/g5_inject.ps1` / `g5_closeout.ps1`
  的 `--fail-encoder` 无参用法（**必须保持兼容**）
- 本机产物：`build-msvc/bin/Release/sr-recorder.exe`（轮 46 基线，已另存 `%TEMP%\sr-r47-baseline\` 作红端）
- 轮 37 立的契约：`--encoder <id>` 不可用 → `exit 6` + 原因、不回退

## 约束

1. **不改 `verify/` 下任何判据与用例**；本卡只改产品代码。
2. 不引入新三方依赖（`vcpkg.json` 只读）；不新增 `CMakeLists.txt` 编译单元/链接库。
3. 自动选择路径的 M3-06「硬编失败切软编且不中断」语义**不得改变**；只收紧被点名的路径。
4. `--fail-encoder` 无参形式（= 全部失败）与 `--help` 的发布包过滤规则保持不变；
   新增 `hardware` / `software` / `all` 可选值仅用于列表一致性反向用例。
5. 探测器不得跨调用缓存失败状态；不得遗留 device / processor / MFT / COM 引用；失败可重试。
6. 不自评「已验收」；覆盖翻绿必须先跑 `V1-020` 的正向 + 反向用例并留红-绿原始输出。

## 完成定义（DoD）

- [ ] 正向：列表每个 `available=1` 项强制指定后 `exit 0` 且 `encoder=<id>`
- [ ] 反向：列表每个 `available=0` 项强制指定后 `exit 6` 且原因非空
- [ ] 注入：`--list-encoders --fail-encoder hardware` 使 `mf-h264-hw` 变 `unavailable` 且原因非空；撤掉注入后恢复
- [ ] 新增断言先红后绿（旧二进制 FAIL、新构建 PASS），旧值已记录
- [ ] MSVC Release/Debug + MinGW Release 零告警；`g5_encoder_list` 等既有回归零失败
- [ ] 四件套齐全，`HANDOFF` 四问答齐，`## 交付状态` 与实际一致

## 覆盖子句

- M3-05#1

## 交付状态

- M3-05#1: 已实现（轮 47：真激活探测 + 强制不回退；`g5_encoder_consistency.ps1` 正向/反向/注入 12 PASS / 0 FAIL）

> 状态迁移留痕（轮 47）：起点 `docs/09` 仍是 108/1/7（轮 46 验收把回退结论写在报告与台账，
> 但未重生成覆盖表）。① 先按裁定落「有卡未实现」→ `coverage_report.py --write` 得 **107/2/7**、
> `--check` PASS（`tasks/G6/V1-020/artifacts/coverage-rollback.txt`）；
> ② 修完列表与开录行为、`g5_encoder_consistency.ps1` 在旧二进制 **4 PASS / 8 FAIL**（红）、
> 新构建 **12 PASS / 0 FAIL**（绿）之后才转「已实现」→ **108/1/7**。
> 两次 `--write` 的原始输出都在 `tasks/G6/V1-020/artifacts/`，没有直接翻绿。
