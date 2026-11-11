# SELFTEST: S5-004 存储守护收口

## 0. 开工四项自查（docs/06 §4.2）

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | PASS | `docs/01-功能需求清单.md` 中 M8-03 / M8-04 / M8-05 均有原文行（子句切分依据见 `verify/tools/coverage_model.py`） |
| 必读章节可定位 | PASS | `docs/05` §6/§7.2、`src/recorder/StorageGuard.h`、`src/core/util/DiskEstimate.h` 均存在且编号可定位 |
| 上游交付物可获取 | PASS | recorder 侧 warn/stop/估算实现已在 `src/recorder/`（既有构建产物 `build-msvc/bin/Release/sr-recorder.exe` 可用）；本卡不改动它 |
| 文件域无交集 | PASS | 本卡文件域 = `src/app/`（S5）+ `verify/cases/g5_storage.ps1` + 本卡目录；与本轮并行改动（`src/app/Elevation.h`、`src/capture/`、`src/compositor/`、`verify/tools/build_release.ps1`）不重叠 |

## 1. 自测命令与输出

```
pwsh -NoProfile -File verify/cases/g5_storage.ps1 -BuildDir build-msvc -Config Release
```

输出（2026-11-08）：

```
RESULT: 13 PASS / 0 FAIL / 0 SKIP
```

逐条明细见 `EVIDENCE.md` §1。

## 2. 构建

```
cmake --build build-msvc --config Release
```

- Release：0 warning / 0 error。
- 改动文件：`src/app/main.cpp`、`src/app/AppConfig.h`、`src/app/AppConfig.cpp`、新增 `verify/cases/g5_storage.ps1`。

## 3. 回归依据

| 回归 | 范围 | 结果 |
| --- | --- | --- |
| `verify/cases/g5_storage.ps1` | 本卡全量 | 13 PASS / 0 FAIL |
| `verify/cases/g3_storage.ps1` | 存储守护既有用例（未被本卡破坏） | PASS |
| `verify/cases/ui_smoke.ps1` ×3 | UI 侧改动（S5 同域） | 见 `tasks/G5/S5-003/SELFTEST.md`（同一轮连跑结果） |

## 4. 交付前自检

- [x] 开工四项自查全部通过并已记录
- [x] 任务卡每条需求 → 一条证据，含命令与输出
- [x] 新断言有红-绿对照（旧行为无该提示 → 新行为有；见 EVIDENCE §4）
- [x] Release 编译 0 告警
- [x] 回归零失败
- [x] 文件域无越界（未改 `src/recorder/`、`src/core/`），`vcpkg.json` 未改，`verify/` 只新增用例未改既有用例
- [x] `HANDOFF.md` 四问齐答，含「哪些需求我没做完」
- [x] 任务卡 `## 交付状态` 已写，且与实际一致
- [x] `## 覆盖子句` 只列实际覆盖的子句
- [x] 已知限制已写明
