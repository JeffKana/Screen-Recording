# HANDOFF: V1-020（轮 47 · 主卡：M3-05 一致性用例 + docs/19 排期 + V1.0 差距盘点 + 回归）

## 1. 我改了什么接口？

**本卡没有产品代码 / 产品接口变更**（产品改动全在 `tasks/G6/S4-008`，见该卡的 `HANDOFF.md`）。
本卡改的是用例、文档与台账：

| 面 | 变化 | 下游影响 |
| --- | --- | --- |
| `verify/cases/g5_encoder_consistency.ps1`（新增） | 12 条断言：正向（可用项真的用上）、反向（不可用项 exit 6）、注入反向（硬件失败 → 列表 unavailable）、重试与资源护栏；支持 `-RecorderPath` 指定旧二进制复跑红端 | 以后改编码器列表/选择行为，先跑这条用例；不要在用例里把「静默回退」重新判成 PASS |
| `verify/cases/g5_format.ps1` | **只**把 `cbr-average-deviation` 的逐秒均值标 `(仅对照)` | 判据/阈值一字未改；`g5_format_selftest` 22/0 |
| `docs/19 §0.B` | 三条由「排轮 47」改为轮 48 / 49 + 卡号 + 可验收交付物；旧表述标为历史 | 轮 48 `S4-006`、轮 49 `S4-007` 按此表立卡；移出 V1.0 需 O0 裁定 + 改 `docs/01` 版本标签 |
| `docs/22`（新增） | 按 `docs/05 §7.2` 的六条判据 + 例外清单 + 子句缺口的四列差距表（判据/当前/差距/归属轮次） | **后续轮次唯一路线图**：每轮收口回写「当前」列，新增差距先回写本表 |
| `docs/INDEX.md`、`docs/07`、`docs/09`、`docs/14` | 登记 22；主表轮 47 留白 + 执行方自述单列；覆盖表由生成器 `--write`（108/1/7）；硬编抽测格改「不可计硬编 PASS」 | 覆盖表状态列**不要手改**，由卡的 `## 交付状态` 驱动 |
| `tasks/G6/S4-008/TASK.md` | **仅状态移交**：用例全绿后 `## 交付状态` 由「有卡未实现」转「已实现」 | 覆盖 107/2/7 → 108/1/7；「最保守合并」规则不变 |

`CONTRACT.md` 未改、未开契约变更单：没有新 IPC 消息、状态机转移或退出码（仍复用码 6）；
产品侧新增的是 `src/encode` 内部的重载与静态方法，既有 `ListEncoders()` 签名与行为不变。

## 2. 下游需要接什么？

1. **验收方**：按 `.codebuddy/plans/轮47指令.md` §通过判据逐条独立重跑。关键命令见
   `SELFTEST.md §1`；红端二进制 SHA256 `795CE22F…5125`（`artifacts/baseline-sha256.txt`），
   `-RecorderPath` 可指向任何待测构建。本卡不自评、不写「已验收」。
2. **规划侧（轮 48 / 49）**：按 `docs/19 §0.B` 立 `S4-006`（MP4 writer + faststart + `--format mp4`，
   同时收口 M3-09#3）与 `S4-007`（MKV 多音轨）；两卡文件域仍是 `src/mux/` + `src/recorder/`。
3. **用例维护者（需一个裁决）**：`g5_closeout.ps1:287` 把
   「不可用行 == 3」写死，轮 47 列表诚实后为 4（新增 `mf-h264-hw unavailable`），
   附加抽样实测 `17 PASS / 1 FAIL`。本卡文件域**未授权**改该用例，也没改判据；
   建议 remedy：把该断言改为「三厂商 id 各命中且各自 `available=0` + 非空 `reason`」，
   或把计数改为 `≥3` 且限定 `nvenc/qsv/amf` 三行；由验收/规划侧决定是否单独开卡，
   **执行方不自行改尺子**。
4. **发布复核轮（建议轮 50）**：`docs/22 §1` 的 LT/ER 三次、兼容性矩阵 10 格、
   B 机性能基线、EX-01（M1-27#1）与 M6-06 终审；真机 BLOCKED 四条按 `docs/19 §0.A` 复测条件触发。
5. **用例维护者（矩阵）**：硬编抽测格只在有真可用硬编 MFT 的机器上才能补三件套；
   本机 `exit 6` 不算硬编 PASS，也别把轮 46 的软编三件套 6/6 当成硬编证据。

## 3. 已知限制是什么？

- **本机没有可用硬件 H.264 MFT**：真激活探测在协商阶段就被拒
  （`encoder rejected NV12 input type`），因此 `mf-h264-hw` 诚实标 `unavailable`、
  强制指定 `exit 6`。这是**产品反馈一致性**的修复，不是把硬编做出来；硬编能力仍需真机。
- **探测器不碰 D3D**：产品编码路径只吃 CPU NV12（`MfVideoEncoder::SubmitBytes`），
  探测器同样不创建/不持有 D3D device 或 video processor，D-25（转换器绑在已释放设备上）
  在本卡没有泄漏面；COM 用 `ComPtr`，`MFStartup/MFShutdown`、`CoInitializeEx/CoUninitialize`
  在失败分支也成对释放（跳过硬件 MFT 激活后 5 轮句柄 204→204，增长 0）。
- **第三方 MFT 激活的驱动侧句柄**：同一进程里真实激活硬件 MFT 会有 **+3 句柄/次**
  （5 轮 268→280），`IMFActivate::ShutdownObject()`、`CoFreeUnusedLibraries()`、
  独立 COM 单元线程均不能消除，属第三方 MFT 的 `ActivateObject` 内部行为、进程退出即回收。
  产品路径每次探测都在**一次性子进程**完成（UI 设置页 spawn `sr-recorder --list-encoders`，
  CLI 本身一跑即退），不会在长驻进程里累积；用例只做上限护栏并如实记录数值。
- **探测配置是代表性的**：`ProbeAvailable` 用 1280×720@30 / 6000kbps CBR，
  与 `--record` 默认档同量级，但不等价于所有分辨率/帧率/码率组合；
  能力相关的组合未逐项探测（M3-05#1 判据是「列表与本次开录结果一致」）。
- **`g5_format` 单秒过冲方差**：首次 14/1（12,799kbps），清场复跑 15/0（峰值 6,000），
  与轮 44/45 记录同源；两次原始输出都留档，没有改判据。
- **降级验证**：矩阵单屏格用 `--display 0` 代表单屏（物理双屏未禁用）；本机 Win11 build 26200
  不是 `docs/05 §1` 的 B 机；150% DPI / 三厂商硬编 / Win10 版本均缺真机。
- **CI 只跑静态检查与双构建**：集成用例由本机与验收方重跑；CI 绿不等于集成验收通过。

## 4. 哪些需求我没做完？

- **M3-09#1 / #2 / #3 未实现**：MP4 muxer / MKV 多音轨 / `--format` 入口都未开工，
  本轮只按轮 47 指令 §P0-2 把轮次落到 48–49 并写清交付物。
- **M1-27#1 未终审**：UAC 安全桌面黑屏侧仍 BLOCKED，EX-01 继续有效；本轮只在差距表写状态。
- **M6-06（150% DPI / 混合 DPI）未终审**：覆盖表记「已实现」与矩阵「未测」的口径需在发布复核轮对齐。
- **M6-01#1 / M6-04#1 / M6-05#1 / M6-08#1 未测**：BLOCKED，缺参考机型 / OS / 混合显卡真机。
- **LT-01 / LT-02 各 3 次、ER-01 / ER-04 发布复核重跑、兼容性矩阵 10 格、B 机性能基线**：均未闭环，
  `docs/22` 逐条给了归属轮次；V1.0 **仍不可发布**。
- **P1 硬编矩阵格没有转 PASS**：本机无硬编成片、三件套不可执行，如实保留非 PASS。
- **附加抽样 `g5_closeout` 17/1**：用例侧过期断言未修（无授权），见 §2 #3。
- **验收结论**：本卡不写。M3-05#1 是否判 PASS 由验收方独立重跑后给。

## 2.1 交付通道状态（如实记录，非验收结论）

- 分支：`round/47`，从 `main` @ `560087a` 拉出，已 `git push -u origin round/47`。
- **PR #7**：<https://github.com/JeffKana/Screen-Recording/pull/7>（`base=main`、`head=round/47`），
  描述按 `.github/pull_request_template.md` 五项填写（任务卡 / 需求→证据 / 自测原始行 /
  未做与挂账 / 风险与回退）。`gh pr create` 因本机 `api.github.com` GraphQL 连接失败与
  keyring 令牌不可读而不可用，改用 `gh auth token` + GitHub REST `POST /pulls` 创建，PR 元数据已核验。
- **CI 三项全绿**（`GET /commits/724d2f1/check-runs` 轮询）：
  `checks (contract/coverage/cards/domain)` success、
  `build (msvc, warnings=0)` success、`build (mingw, warnings=0)` success。
- CI 只跑静态检查与双构建；集成用例（含 12 条一致性断言与规定回归集）由本机与验收方重跑，
  原始 RESULT 行见 `SELFTEST.md §1/§3`，**不因 CI 绿就写成集成验收通过**。
