# EVIDENCE: V1-018（轮 45 · 主卡）

> 口径声明：本卡不改产品与用例，只做文档回写与全量回归；**不写「已验收」**。
> 数据来源：2026-11-11 20:18~21:10 / `build-msvc`（VS2022）/ Win11 build 26200 / i5-13500 / 1920x1080 / DPI 100%。
> 工具链：CMake 3.3x + MSVC 19.x（Release/Debug）、MinGW-w64 UCRT（Release）。

## 1. `docs/19` 7 条逐条定性（P0）

- 验证方式：把「需真机/新容器」这种模糊表述压成两选一（`BLOCKED（缺什么）` / `V1.0 未开工（排哪轮）`），
  并让 `docs/09` 最后一列与之一致。
- 命令：`rg '待排期|需新容器' docs/19-轮44-收口可行性与判据矛盾核查.md`（应无命中）
- 输出：`docs/19 §0`（定性表）+ `docs/09` 挂账列
- 结论：

| 子句 | 定性 | 缺什么 / 排哪轮 |
| --- | --- | --- |
| M3-09#1 | V1.0 未开工（可做） | 排轮 46，`S4-006`（MP4 封装器） |
| M3-09#2 | V1.0 未开工（可做） | 排轮 46，`S4-007`（MKV 多音轨） |
| M3-09#3 | V1.0 未开工（可做） | 排轮 46，随 `S4-006` 收口（`--format` 入口） |
| M6-01#1 | BLOCKED | 缺 i5-12400（4C8T）机型 + `docs/05 §2.1` 10 分钟独占窗口 |
| M6-04#1 | BLOCKED | 缺 NVIDIA 机型、缺 AMD 机型（本机只有 Intel UHD 770 + 2 虚拟显示器） |
| M6-05#1 | BLOCKED | 缺 Win10 1903、Win10 21H2、Win11 23H2 三台机器 |
| M6-08#1 | BLOCKED | 缺带独显的混合显卡机型 |

## 2. 覆盖表状态迁移（M3-04#1 回退）

- 验证方式：`tasks/G6/V1-014/TASK.md` 的 `## 交付状态` 改为「有卡未实现」后重生成。
- 命令：`python verify/tools/coverage_report.py --write` → 再 `--check`
- 输出：
  - `docs/09` 头部统计行：`已实现 **107**，有卡未实现 **2**，无卡 **7**，未实现已挂账 **8**`
  - `python verify/tools/coverage_report.py --check` → `coverage table is up to date`
- 结论：**通过**（108/1/7 → **107/2/7**，回退属实，不是回归）。

## 3. 四项静态检查

| 检查 | 命令 | 结果 |
| --- | --- | --- |
| 契约 | `python verify/tools/contract_check.py` | `汇总: 8 项检查, 8 PASS, 0 FAIL` |
| 覆盖 | `python verify/tools/coverage_report.py --check` | `coverage table is up to date` |
| 卡面 | `python verify/tools/card_consistency_check.py` | `OK —— 扫描 56 张卡，0 处冲突` |
| 文件域 | `python verify/tools/domain_table_check.py` | `MISMATCH_COUNT=0` |

## 4. 矩阵与终审材料（P1）

| 项 | 结论 | 证据 |
| --- | --- | --- |
| `docs/14` 矩阵补格 | 双屏 100% 格补 **dxgi** 后端实测（2026-11-11）；**仍未新增独立格**并写明原因（`printwindow` 只接窗口采集） | `tasks/G6/V1-017/artifacts/r45-matrix.txt`、`docs/14 §3/§5` |
| `docs/18` M1-27 终审 | 新增 §6「轮 45 定稿状态」：EX-01 措辞未改、M1-27#1 仍「有卡未实现」、黑屏一侧仍 BLOCKED（缺安全桌面机器） | `docs/18 §6` |
| `docs/07` 台账 | 新增「轮 45 状态迁移留痕」+「轮 45 记录」（P0/P1/未达成三节，含原始数值） | `docs/07` |
| `docs/17` 缺陷单 | 新增 §8「轮 45 实测结果」：三选二全部无效、逐帧 QP 被拒、实验代码已回滚 | `docs/17 §8` |

## 5. 回归（轮 45 §4 判据逐条）

| 用例 | 判据 | 实测 | 时间 |
| --- | --- | --- | --- |
| `g5_storage` | 14 | `14 PASS / 0 FAIL / 0 SKIP` | 20:56 |
| `g5_r43_closeout` | 12 | `12 PASS / 0 FAIL / 0 SKIP` | 20:55 |
| `g5_window` | 24 | `24 PASS / 0 FAIL / 0 SKIP` | 20:56 |
| `g5_uac` | 17+1SKIP | `17 PASS / 0 FAIL / 1 SKIP` | 20:57 |
| `ui_smoke` ×3 | 3×103 | `103 PASS / 0 FAIL` ×3 | 20:58~21:00 |
| `g5_format_selftest` | ≥19 | `19 PASS / 0 FAIL` | 20:54 |
| `g5_encoder_list` ×5 | 17/0 | `17 PASS / 0 FAIL` ×5（见 `V1-017`） | 20:50 |
| `g5_format` | 全绿 | `14 PASS / 1 FAIL`（唯一 FAIL = M3-04#1 滑窗 21.9%，属产品侧，不掩盖） | 20:18 |

## 6. 构建与出包

| 项 | 命令 | 结果 |
| --- | --- | --- |
| MSVC Release | `cmake --build build-msvc --config Release` | 0 error / 0 warning |
| MSVC Debug | `cmake --build build-msvc --config Debug` | 0 error / 0 warning（`warning C` 计数 0） |
| MinGW Release | `cmake --build build-mingw` | 0 error / 0 warning（`warning:` 计数 0） |
| 发布包 | `pwsh -File verify/tools/build_release.ps1` | `RESULT: 17 PASS / 0 FAIL`；`dist/` 只有 `ScreenRecorder-0.1.0-win-x64.zip` + `release-summary.json`；包内 exe SHA256 == 构建目录 SHA256 |
