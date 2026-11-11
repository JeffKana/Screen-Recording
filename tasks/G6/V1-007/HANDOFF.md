# HANDOFF: V1-007（轮 43 主卡）

## 一、我改了什么接口？

**无接口签名变更**，`CONTRACT.md` / `vcpkg.json` / 合并规则一字未改。

| 改动 | 性质 |
| --- | --- |
| `verify/cases/g5_cbr_probe.ps1`、`verify/tools/cbr_nal_probe.py`（新增） | F1 定性：5+5 次 CBR 40s + 逐秒表 + metrics + NAL 级字节分解 |
| `verify/cases/g5_format.ps1`（V1-008） | F2 取数三档语义、重试一次、SKIP；`RESULT` 行加 SKIP 计数 |
| `verify/cases/g5_format_selftest.ps1`（V1-008，新增） | 17 条自检（含 7 条「判据未改软」文本护栏） |
| `verify/cases/g5_r43_closeout.ps1`（V1-010，新增） | P1 四条收口的 12 条行为断言 |
| `tasks/G6/V1-010/artifacts/ColorWindow.ps1`（新增） | 受控纯色全屏背景（独立 STA 进程，保证消息循环） |
| `src/recorder/main.cpp`、`RecordingSession.h/.cpp`（S1-011） | `--cursor on|off` 解析/help/stdout 字段/透传到 `CaptureRequest::include_cursor` |
| `src/app/main.cpp`（S5-006） | 自绘红点 HICON + 分层边框窗口 + `UpdateRecordIndicator` + `--record-border` + 注入开关 + 像素自检日志 |
| `src/encode/MfVideoEncoder.cpp/.h`（S4-004） | **曾改为「按秒结算」，实测不适用后全部回滚到轮 42 原状**（当前无改动） |
| `docs/07`、`docs/09`、`docs/14`、新增 `docs/17`、`docs/18` | 台账、覆盖表、矩阵、CBR 缺陷单、M1-27 终审材料 |
| 7 张任务卡 + 四件套 | `V1-007/008/009/010`、`S4-004`、`S1-011`、`S5-006` |
| `dist/*.zip` + `release-summary.json` | 源码改动后由 `build_release.ps1` 重建（未用 `-SkipBuild`） |

## 二、下游需要接什么？

1. **验收方独立重跑**（执行方不自评）：
   ```
   pwsh -File verify/cases/g5_format_selftest.ps1          # 17/17
   pwsh -File verify/cases/g5_format.ps1 -SimulateNoStart  # 1 SKIP / 0 FAIL
   pwsh -File verify/cases/g5_r43_closeout.ps1             # 12/12
   pwsh -File verify/cases/g5_cbr_probe.ps1 -Seconds 40 -Runs 5 -Mode both   # 约 8 分钟
   python verify/tools/coverage_report.py --check
   python verify/tools/contract_check.py
   python verify/tools/card_consistency_check.py
   python verify/tools/domain_table_check.py
   ```
2. **必须从 `docs/17 §6` 的 A/B/C/D 中裁定 F1 方向**：
   A 允许在 S4 域收紧码控层（需新卡）；B 保持 M3-04#1「有卡未实现」；
   C 修订「逐秒 ≤10%」的需求口径；D 认定已满足（需验收方独立重跑 10 次全 ≤10%）。
   执行方**未自行改 CBR 模式实现、未自行改需求判据**。
3. 若选 A 并成功，下一轮把 `tasks/G6/V1-009/TASK.md` 的 `## 交付状态` 改为「已实现」，
   跑 `--write` + `--check`（第 2 次）并补台账翻绿备注。
4. 矩阵：轮 44 需在能命中 dxgi-dd 的机器上补第二后端；本机请求 `dxgi-dd` 实际
   `used=wgc`（自动回退），已标未测、不计 PASS。
5. M1-27：`docs/18` 的 6 步人工复测步骤需要安全桌面机器，供轮 44 终审。

## 三、已知限制是什么？

1. **F1 修复未达成且已回滚**：按秒结算的 10 次实测静态 3/5 超、运动 5/5 超，
   运动 avg 从 6,276 → 8,039 kbps（偏差 34%）引入新回归。物理原因：filler 是只增不减的
   H.264 NAL，某秒真实编码输出（IDR + 运动 P 帧）超 750,000 B 时无法削回。
   **不为保住覆盖数字留一个让产品变差的改动**，M3-04#1 保持 PARTIAL。
2. **状态迁移只完成回退一次 `--check`**，翻绿那次因修复不成立而未执行。
3. **代理/降级验证**：M7-13 用注入状态走同一 UI 渲染分支（非真实录制会话）；
   M1-24 全画面是抽样（步长 2）；矩阵首格用 `--display 0` 代表单屏（物理双屏）。
4. **静态定性极端值**（359% / 247%）受本机 IDE/终端窗口重绘影响；结论不依赖它们
   （运动源 5/5 稳定超 10% 与静态 59% 已足够）。
5. **V1.0 仍不可发布**：7 条无卡、M3-04#1 PARTIAL、M1-27 待终审、M6-06 BLOCKED、
   矩阵 11 格未按三件套实测、`docs/05 §7.2` 其余判据未跑完。

## 四、哪些需求我**没**做完？（最重要）

| 项 | 状态 | 原因 / 下一步 |
| --- | --- | --- |
| **P0 F1 CBR 过冲定性** | 已交付 | 5+5 次逐秒表 + NAL 分解；结论成立 → 照实回退 M3-04#1 |
| **P0 F1 修复（10 次全 ≤10%）** | **没做完，已回滚** | 指令指定的 filler 按秒结算经 10 次实测不适用且引入 avg +34% 回归；根因在码控层瞬时输出，需 `docs/17 §6` 裁定 |
| **P0 M3-04#1 回退再翻绿（两次 `--check`）** | **只完成回退** | 翻绿条件未满足，保持「有卡未实现」，不假绿 |
| **P0 F2 取数失败分离** | 已交付 | 1 SKIP / 0 FAIL / exit 0；产品失败不伪装 SKIP；判据一字未改 |
| **P1 无卡收口 4 条** | 已交付 | 12/12 断言；覆盖 **107 / 2 / 7**（≥107 / ≤8 达成） |
| **P1 矩阵补齐** | 部分交付 | wgc 后端实测 PASS；dxgi-dd 自动回退到 wgc → 该后端标未测；其余格仍未测 |
| **P1 M1-27 终审材料** | 已交付 | `docs/18` 一页纸，待轮 44 终审 |
| `g5_format` 连跑 3 次全绿（指令 §3 #3） | **未达成，PARTIAL** | 实测 14/14、14/14、**13/1**（`m3-04-cbr-spread` 逐秒 6,000~16,699kbps、spread 154.7%）。这是 F1 未修复的真实产品失败，不放宽判据、不把真实失败藏成 SKIP |
| 全量回归「零失败」 | **未达成，PARTIAL** | 除上述外，`g5_r43_closeout` 回归首轮 11/1（WGC 光标层时序，修后独立 3×12/12）；其余 19 项 + `ui_smoke` 3×103 + 发布包 17/17 全绿 |
| 剩余 7 条无卡（M3-09#1/#2/#3、M6-01#1、M6-04#1、M6-05#1、M6-08#1） | **没收口** | 需 MP4/多音轨容器、4核8线程参考机型、三厂商真机、三个 Windows 版本、混合显卡；不拿文案或降级冒充实测 |
| M7-13 真实录制会话终审 | **没做** | 需「启动真实录制 → 暂停 → 停止」的 UI 用例，轮 44 |
| **V1.0 发布** | **仍不可发布** | 见第三问 |

## 五、交付包清单

```
tasks/G6/V1-007/  TASK.md EVIDENCE.md SELFTEST.md HANDOFF.md + artifacts/（定性/矩阵/回归/构建）
tasks/G5/S4-004/  四件套 + artifacts/cbr-after-fix/（F1 修复实测）
tasks/G6/V1-008/  四件套 + artifacts/f2-selftest.txt
tasks/G6/V1-009/  四件套（状态回退）
tasks/G6/V1-010/  四件套 + artifacts/ColorWindow.ps1
tasks/G5/S1-011/  四件套（--cursor）
tasks/G5/S5-006/  四件套（红点/边框）
文档：docs/07、docs/09、docs/14、docs/17、docs/18
```

**执行方不自评为已验收**；所有结论待验收方独立重跑判定。
