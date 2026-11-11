# SELFTEST: S5-003（M1-27）

## 0. 开工四项自查（docs/06 §4.2）

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | PASS | `docs/01-功能需求清单.md` 有 M1-27 行；子句切分见 `verify/tools/coverage_model.py` |
| 必读章节可定位 | PASS | `docs/01` M1-27、`docs/02` §7、`docs/03` §4、`CONTRACT.md` §4 均可定位 |
| 上游交付物可获取 | PASS | 采集侧既有实现（`src/capture/`）+ 既有构建产物 `build-msvc/bin/Release/*.exe` 可用 |
| 文件域无交集 | PASS | 本卡文件域 = `src/app/`（S5）+ `verify/cases/g5_uac.ps1` + 本卡目录；与同轮并行的 `src/capture/`、`src/compositor/`、`src/tools/srprobe/`、`verify/tools/build_release.ps1` 不重叠（`src/tools/srprobe/` 的 `--hwnd-file` 改动归 S1-008 卡） |

## 1. 自测命令与输出

```
pwsh -NoProfile -File verify/cases/g5_uac.ps1 -BuildDir build-msvc -Config Release
```

输出（2026-11-08）：`RESULT: 17 PASS / 0 FAIL / 1 SKIP`（SKIP 为「黑屏一侧本机不可复现」，附实测数据）。

## 2. 构建

```
cmake --build build-msvc --config Release
```

- Release：0 warning / 0 error（修掉了 `wprintf` 的 C4477 格式告警）。
- 新增/改动：`src/app/Elevation.h`（新增）、`src/app/main.cpp`、`src/tools/srprobe/window_host.cpp`（`--hwnd-file`，归 S1-008）。

## 3. 回归

| 回归 | 结果 |
| --- | --- |
| `verify/cases/g5_uac.ps1` | 17 PASS / 0 FAIL / 1 SKIP |
| `verify/cases/ui_smoke.ps1` 连跑 3 次 | 103 PASS / 0 FAIL / 0 SKIP ×3（结果见 `verify/artifacts/ui_smoke_x3.txt`） |
| `verify/cases/g5_window.ps1` | 见 `tasks/G5/S1-008/EVIDENCE.md`（同轮重跑） |
| `verify/cases/g5_storage.ps1` | 13 PASS / 0 FAIL（同轮新增，S5-004） |

## 4. 交付前自检

- [x] 开工四项自查已记录
- [x] 一条需求点一条证据（含命令与输出）
- [x] Release 编译 0 告警
- [x] 回归零失败（ui_smoke ×3 全绿）
- [x] 文件域无越界；`verify/` 只新增用例未改既有用例
- [x] `HANDOFF.md` 四问齐答，含「哪些需求我没做完」
- [x] `## 交付状态` 与实际一致（M1-27#1 仍是「有卡未实现」，**没有**自我拔高）
- [x] 提权自动化不自动弹 UAC：由用户确认触发，脚本只等日志
- [x] 已知限制已写明
