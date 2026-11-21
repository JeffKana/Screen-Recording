# HANDOFF · V1-024（轮 52 · faststart 预留回填 + 长稳第 3 次 + F3-42 定向复现）

## 1. 我做了什么

| 类 | 内容 |
| --- | --- |
| P0-1 faststart | `Mp4Writer` 改为「开录预留 `free` box + 收尾把 moov 回填进预留区」：`mdat` 一个字节都不搬，收尾从 O(文件大小) 降到 O(moov)。预留按 `--seconds`/`--fps` 估 moov 上界（16B/样本 + 4KB，8 字节对齐，上限 64MB）；装不下时回退旧搬移路径并打 `faststart_fallback=move` |
| 观测 | 收尾日志新增 `faststart_mode=reserve\|move` / `faststart_reserve` / `faststart_moov_bytes` / `faststart_close_us`；调试开关 `--mp4-reserve <bytes>`（`SR_TEST_HOOKS=ON`） |
| P0-2 | LT-02 第 3 次 **8 PASS / 0 FAIL**（已出结果）；LT-01 第 3 次 **13:37:07 启动、交付时运行中** |
| P0-3 | `F3-42` 定向复现：三场景各 10 次（另跑一轮合计 60 次场景执行、110 条进程级记录），**静默退出 0 次** → 级别保持「未定性观察」，`docs/26 §3.1` |
| P1 | `g5_mp4` 判据拆两条（通用 `moov<mdat` / 短素材 ≤60s 前 64KB）+ 4 条新断言（19 → 23 条）；`mp4_box_check.py` 按 `mvhd.duration` 分流；issue 模板演练完成（**#18**，已关闭）；`docs/22` 判据 ②③ 数字回写 |
| 新发现 | `R1-52`：两个 `--record` 进程在同一毫秒窗口抢同一个 `--out` → `.part` 互写、产物 EBML 损坏（两进程 `exit=0`）。**未修**（属 `src/recorder/OutputPath.cpp` 原子占位，超出本卡文件域），提单待定级 |

## 2. 我是怎么验证的

- 一条需求一条证据：`EVIDENCE.md §1`（faststart 五条：① 0.976ms / ② 8h 外推 0.02s / ③ 布局+全解零 error / ④ 回退可播 / ⑤ g5_mp4 23/0 + g3_playback 11/0）、`§3`（F3-42）、`§4`（LT）、`§5`（issue 演练）。
- 先红后绿：`EVIDENCE.md §2.3` —— 用轮 51 的旧构建跑**新**用例得 `20 PASS / 3 FAIL`，本轮构建 `23 PASS / 0 FAIL`。
- 门槛：`SELFTEST.md §3/§4` —— `g5_mp4` 23/0、`g5_multiaudio` 10/0、`g5_inject` 28/0、`g4_gate` 12/0、`g3_playback` 11/0、`ui_smoke` 3×103/0/0、契约 8/8、覆盖 111/1/4、三构建零告警。
- 顺序声明：**所有用例、构建、回归都在 LT-01 启动之前完成**；LT-01 于 13:37 启动后只做网络类操作（git/gh）与文本编辑，未再跑任何录制/构建/用例。

## 3. 哪些需求我**没**做完（逐条，不得计为通过）

| 项 | 状态 | 说明 / 谁来补 |
| --- | --- | --- |
| **LT-01 第 3 次（8h）** | **未完成（运行中）** | 2026-11-21 13:37:07 启动，预计 21:37 出 `verify/artifacts/lt01-real-20261121-133707/summary.json`。结果出来前，**判据 ③ 的 LT-01 仍按 2/3 计**，不得据此判通过 |
| **`R1-52` 修复** | **未做（只记单）** | 属 `.part` 原子占位改造（`src/recorder/OutputPath.cpp`），超出轮 52 文件域；级别待裁，排期由验收方/O0 定 |
| **四条真机（M6-01/04/05/08）+ `M1-27#1` 黑屏侧** | **未做** | 0.1 范围外（`docs/27 §3` EX-02~EX-06），1.0 前置 |
| **`docs/09` 挂账理由的 0.1 口径** | **未做（轮 53 的活）** | 本轮只改 `docs/22` 数字；`docs/09` 的「缺机器待排期」措辞改写属轮 53 P0-1 |
| **包/发布** | **未做（轮 53）** | 0.1.0 出包、干净目录冒烟与 release 见轮 53 |
| **MSVC Release 的 `--mp4-reserve`** | **只存在于 hooks ON 构建** | 交付包（`SR_TEST_HOOKS=OFF`）不含该开关；验收重跑判据 ④ 请用 `build-msvc` |

## 4. 三件需要验收方裁定的事

### 4.1 `R1-52` 的级别（建议「一般」）

- 现象与证据见 `EVIDENCE.md §3.1`：并发抢同一 `--out` → 2/20 记录产物 EBML 损坏、两进程 `exit=0`。
- 为什么建议不是「阻塞」：`docs/05 §7.1` 的示例里「文件被静默覆盖」属阻塞，但**用户可见路径不可达**
  （UI 输出名由 UI 生成、需刻意并发两条命令且 `--out` 完全相同）。**级别请验收方定**。
- 若判阻塞：0.1 的「阻塞 0」（`docs/27 §4` 第 1 条）不成立 → 0.1 不应发布，先修 `OutputPath`。

### 4.2 预留区的"未用完空间"是否可接受

- 现状：预留按预估时长算，没用完的部分以一个顶层 `free` box 留在 moov 与 mdat 之间。
  实测：5s 素材 13,760B（文件 1.81MB 的 0.76%）、240s/1.12GB 素材 110,448B（0.009%）。
- 取舍：不回收 → 短录制文件偏大一点；回收 → 需在收尾把 mdat 整体前移（O(文件大小)，正是本轮要消灭的）。
- 本轮选「不回收」。若验收方认为短素材的膨胀不可接受，可在下一轮加「小于阈值时才回收」的折中（代价是再引入一条路径）。

### 4.3 判据拆分的口径确认

- 已按 `验收报告-轮51 §R3` 落地：通用 `moov` 偏移 < `mdat` 偏移；短素材（≤60s）`moov` 结束 ≤64KB。
- `mp4_box_check.py` 对 >60s 素材**打印 SKIP**（不判 PASS/FAIL）。如果验收方希望「长素材也判一条」，
  建议用「moov 体积 ≤ 样本数 × 每样本上限」这类可实现的口径，而不是回到 64KB。

## 5. 验收方请重跑这些（都能一条命令复现）

```powershell
# 判据 ① ② ③：1.1GB/240s 实测（约 9 分钟，两次录制）
pwsh -File verify/cases/g5_faststart_perf.ps1 -Seconds 240 -BitrateKbps 40000
# 判据 ④ ⑤ + 判据拆分：23 条
pwsh -File verify/cases/g5_mp4.ps1
# 红端（旧构建对照）：20 PASS / 3 FAIL
pwsh -File verify/cases/g5_mp4.ps1 -RecorderPath build-release\bin\Release\sr-recorder.exe
# F3-42 定向复现（约 3 分钟）
pwsh -File verify/cases/g5_f342_repro.ps1 -Iterations 10
# LT-01 第 3 次：等 21:37 后看 verify/artifacts/lt01-real-20261121-133707/summary.json
```
