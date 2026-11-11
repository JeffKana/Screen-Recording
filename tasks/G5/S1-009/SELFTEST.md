# SELFTEST: S1-009（轮 41 · 发布包 `--help` 补全）

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | 本卡不新增子句；上游 M1-07/M1-08/M1-09 原文在 `docs/01:100-102`，由 `tasks/G5/S2-002` 覆盖 |
| 必读章节可定位 | 通过 | `.codebuddy/plans/轮41指令.md §1/§3/§4.4`、`验收报告-轮40.md §3`、`docs/11 §4`、`CONTRACT §2` 均在位 |
| 上游交付物可获取 | 通过 | 轮 40 `tasks/G6/V1-001/` 首版 zip、`dist/release-summary.json`（`zip_sha_matches_build=true`）、`src/recorder/main.cpp:105-250` |
| 文件域无交集 | 通过 | 本卡仅 `src/recorder/main.cpp` + `tasks/G5/S1-009/*`；打包脚本断言与文档由 V1-002 卡承接 |

## 1. 自测命令与输出

| 命令 | 结果 | 证据 |
| --- | --- | --- |
| `pwsh -File verify/tools/build_release.ps1` | `17 PASS / 0 FAIL`，`BUILD_RELEASE_EXIT=0` | `tasks/G6/V1-002/artifacts/build-release.txt` |
| `Expand-Archive dist/…zip` + 包内 `sr-recorder.exe --help` | 四参数齐全，`--window` 适用条件写清 | `artifacts/package-help.txt:13-17` |
| 发布 help 调试开关残留检查 | `release-help-no-debug-switch` / `extract-help-no-debug` 均 PASS，残留 0 | 同上 |
| `--version` | `test_hooks=0`、`build release` | 同上 |
| 包内 / 构建目录 SHA256 | `zip-ui-sha-matches-build`、`zip-recorder-sha-matches-build` 均 PASS | 同上 |

## 2. 红-绿对照

| 新断言 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| 包内 `--help` 含 `--record`/`--window`/`--fit`/`--region` | 轮 40 验收方独立重跑记录：`--help` 未列出这些参数（R4）；但包内 exe 实测 `--record` 能录（`record_exit=0`），功能一直在 | `artifacts/package-help.txt:13-17` 四个串齐全 |
| `release-help-four-params` | 断言不存在（轮 40 脚本只查调试开关） | `PASS`，缺 0 |
| `extract-help-four-params` | 断言不存在 | `PASS`，缺 0（解压产物上验，符合判据「包内 exe 的 help」） |

> 红证据来自验收方轮 40 报告的独立实测，不是执行方构造的假红。

## 3. 回归

- `ParseArgs` 未改，四个参数在发布构建下的可用性由轮 40 验收方实测（`--record` 能录）与本轮
  `build_release.ps1` 17/17 共同支撑。
- 调试开关未泄漏：`--help` 里六项调试开关命中 0。
- 三构建与用例全量回归由 `tasks/G6/V1-002/artifacts/run-regression.ps1` 承担，
  结果见该包 `regression.txt`（本卡 `src/recorder/main.cpp` 的改动会影响 MSVC Release/Debug
  与 MinGW 三套编译，均在其中验证零告警）。

## 4. DoD 勾选

- [x] 开工四项自查通过并已记录
- [x] 每条需求 → 一条证据，含命令与输出（`EVIDENCE.md §1~§3`）
- [x] 红-绿对照表完整（旧值 + 新值）
- [x] 发布构建零告警（见 `regression.txt` 的 BUILD 行）、包内 help 四参数、SHA 自证
- [x] 文件域无越界，`vcpkg.json` / `CONTRACT.md` 未改，接口无变更
- [x] 无死代码：`#if SR_TEST_HOOKS` 调试段只调首行措辞避免 `--record` 重复，调试开关一个没删
- [x] `## 覆盖子句` 写「无」，`## 交付状态` 与实际一致
- [x] 已知限制已写明（`EVIDENCE.md §6`）
- [x] 未自评为已验收
