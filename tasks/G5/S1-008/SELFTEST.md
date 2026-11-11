# SELFTEST: S1-008

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | PASS | `docs/01` M1-08 / M1-27 |
| 必读章节可定位 | PASS | `CONTRACT.md` §2（文件域分工）、§4（`CaptureRequest` / `ICaptureSource`） |
| 上游交付物可获取 | PASS | 轮 39 的实现与 `build-msvc` 产物均在；`tasks/G5/S2-002/HANDOFF.md` 记录了机制与复现 |
| 文件域无交集 | PASS | 本卡只动 `src/recorder/`、`src/tools/srprobe/`（均属 S1）；与 S5 卡（`src/app/`）、S2 卡（`src/capture/`、`src/compositor/`）不重叠 |

## 1. 自测命令与输出

```
pwsh -NoProfile -File verify/cases/g5_window.ps1 -BuildDir build-msvc -Config Release
```

输出（2026-11-08）：`RESULT: 24 PASS / 0 FAIL / 0 SKIP`

## 2. 构建

```
cmake --build build-msvc --config Release
```

- Release：0 warning / 0 error。
- 本轮实际改动：`src/tools/srprobe/window_host.cpp`（`--hwnd-file`，+4 行）。

## 3. 回归

| 回归 | 结果 |
| --- | --- |
| `verify/cases/g5_window.ps1` | 24 PASS / 0 FAIL / 0 SKIP |
| `verify/cases/g5_uac.ps1`（依赖本卡的 `--hwnd-file`） | 17 PASS / 0 FAIL / 1 SKIP |

## 4. 交付前自检

- [x] 开工四项自查已记录
- [x] 一条子句一条证据
- [x] Release 编译 0 告警
- [x] 回归零失败
- [x] 文件域无越界（只动 S1 自己的目录）
- [x] `HANDOFF.md` 四问齐答
- [x] `## 交付状态` 与实际一致
- [x] 已知限制已写明
