# HANDOFF: V1-019（轮 46 · 主卡：M3-04#1 判据落地 + 收口）

## 1. 我改了什么接口？

**没有产品代码 / 产品接口变更**：`src/` 净改动 0（可用 `git diff --stat main...round/46 -- src/` 核验），
`CONTRACT.md` 未改，未开契约变更单。改动都在判据文档、用例与台账：

| 面 | 变化 | 下游影响 |
| --- | --- | --- |
| `docs/01` M3-04 验收要点第 1 句 | 按 `docs/21 §2` 替换为「全片平均 ≤10% + 单秒 ≤2×」，逐秒/滑窗方差不作判据 | 所有引用 5s 滑窗口径的历史文档/用例需按新判据解读；`docs/09` 已重生成 |
| `verify/lib/CbrCriteria.ps1`（新增） | 导出 `Get-CbrSecondBuckets` / `Get-CbrVerdict` / `Start-CbrActiveDesktop` / `Stop-CbrActiveDesktop` | `g5_format.ps1` 与 `g5_cbr_probe.ps1` 共用；**新增用例不得再复制一套分桶或判据** |
| `g5_format.ps1` | 新增 `-CbrScene active`、`-CbrOnly -CbrFile/-CbrStdout`、`-SimulateCbrBytes/-SimulateCbrSecs`；断言 `cbr-window5-spread` → `cbr-average-deviation`，`m3-04-cbr-nominal` → `cbr-average-source-wholefile` | 反向护栏用新参数注入；同场景对照用 `-CbrOnly` |
| `g5_cbr_probe.ps1` | 逐次 `summary.json` 新增 `whole_avg_kbps` / `avg_deviation_pct` / `avg_ok` / `cap_ok` / `fail_kind`；`win5_*` 字段删除 | 读 probe 输出的脚本要按新字段名解析；`fail_kind` 区分 `average` / `single-second` / `average+single-second` |
| `g5_format_selftest.ps1` | 护栏 19 → 22；原始输出改落 `tasks/G6/V1-019/artifacts/` | 不再写 `V1-013/artifacts` |
| `tasks/G6/V1-014/TASK.md` | `## 交付状态` 由「有卡未实现」转「已实现」（**仅状态移交**） | 覆盖表状态由卡面驱动，不能手工改状态列 |

## 2. 下游需要接什么？

1. **验收方**：按 `docs/21 §5` 与轮 46 指令 §通过判据逐条独立重跑。关键命令见 `SELFTEST.md §1`；
   6 次实测的原始产物在 `tasks/G6/V1-019/artifacts/cbr-6runs/`（本地证据，仓库不收 `artifacts/`，
   重跑命令会重新生成同名结构）。本卡不自评，也不在 PR 里写「已验收」。
2. **规划侧（轮 47）**：按 `docs/19 §0.B` 单独立卡 `S4-006`（MP4 muxer：`ftyp/moov/mdat` +
   faststart 重排 + `--format mp4`）与 `S4-007`（MKV 多音轨）；M3-09#3 随 `S4-006` 收口。
   本轮**没有**把 MP4 实现塞进收口轮（轮 46 指令 §注意事项 #4）。
3. **规划侧（真机）**：`docs/19 §0.A` 四条仍缺参考机型 / OS / 显卡组合，复测条件已写清；
   `docs/18 §7` 记录 M1-27 的 §3.1 四条门槛本机一条未满足，EX-01 继续 BLOCKED。
4. **用例维护者**：以后改 CBR 判据只改 `verify/lib/CbrCriteria.ps1` 一处；
   平均口径写死全片 `bytes_written/wall_seconds`，单秒 2× 挡板不许被平均 PASS 稀释。

## 3. 已知限制是什么？

- **ActiveDesktop 默认参数**：`Start-CbrActiveDesktop` 用 `pwsh -NoProfile -File verify/lib/ActiveDesktop.ps1`
  启动，不传 `-Seconds`/`-Windowed`/`-BurstSec`（默认 120s / 全屏 / BurstSec=2）。
  本机活动场景 `capture_fps` 40~47，静态 1.0~13.0；**这是本机可复现的激励强度，不等于所有桌面的活动量**。
- **全片平均含音频轨**：`bytes_written` 是 recorder 写出的总字节数（视频 + 音频），
  因此 6 次实测偏差 3.23%~4.10% 而不是 0%；判据口径按 `docs/21 §4 #2` 就是全片，未做扣除。
- **逐秒/滑窗方差不作判据**：6 次里 static-02/03 的逐秒 spread 达 15.7% / 15.5%，
  按 `docs/21 §2` 明确不参与判定；不要拿它当回归失败。
- **F3 对照是同一成片**：两个工具分析 `active-01.mkv` 得相对差 0.0006%；
  另有一次独立 active 录制（`g5_format -CbrScene active`）全片 6,207kbps vs probe 6,197.4kbps，
  差 0.16%（≤2%），但不同录制的自然抖动不应被当成「实现差异」。
- **M1-27 / 真机矩阵格**：本机家庭版 + UAC 安全桌面关闭、无三厂商独显，属硬环境阻塞，
  不能用降级版本冒充（见 `docs/18 §7`、`docs/19 §0.A`）。
- **CI 只跑静态检查与双构建**：集成用例由本机与验收方重跑；CI 绿不等于集成验收通过。

## 4. 哪些需求我没做完？

- **M3-09#1 / #2 / #3 未实现**：MP4 muxer / MKV 多音轨 / `--format` 入口都未开工，
  已按独立卡排轮 47（本轮只把它们从「真机阻塞」里分离出来并给轮次）。
- **M6-01#1 / M6-04#1 / M6-05#1 / M6-08#1 未测**：BLOCKED，缺参考机型、OS 版本与混合显卡真机。
- **M1-27#1 未翻绿**：UAC 安全桌面黑屏一侧仍 BLOCKED，`docs/18 §3.1` 复测门槛一条未满足，
  本轮只补了「轮 46 复核记录」，没有做终审、没有判 PASS。
- **M6-06（混合 DPI）与其余发布判据**：本轮未涉及，`docs/05 §7.2` 六条未终审，V1.0 仍不可发布。
- **本卡范围内的 P1 矩阵补格**：以 `SELFTEST.md §1` 的 `r46_matrix_hw` 原始 RESULT 行为准；
  若该项未绿，如实记在 `EVIDENCE.md` 的未完成段，不冒充完成。
- **验收结论**：本卡不写。M3-04#1 是否判 PASS 由验收方独立重跑后给。
