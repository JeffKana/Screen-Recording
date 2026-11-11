# EVIDENCE: S1-009（轮 41 · 发布包 `--help` 补全，R4）

> 口径声明：验收方轮 40 报告 §3 已**证伪**「发布包陈旧、没有 M1-08」的假设——
> 包内 exe 实测 `--record` 能录（`record_exit=0`），二进制里 `--window/--fit/--record` 串都在。
> 包是新的，唯一问题是 `--help` 没列出这些参数（**用户不可发现**）。本卡只修帮助文本，
> **不改解析逻辑、不声称此前功能缺失**。
> 数据来源：2026-11-09，`build-release/bin/Release`（`SR_TEST_HOOKS=OFF` / `SR_BUILD_TOOLS=OFF`）。
> 证据：`tasks/G6/V1-002/artifacts/package-help.txt`、`build-release.txt`。

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | 本卡不新增/不改写需求子句；上游 M1-07/M1-08/M1-09 的原文在 `docs/01:100-102`，已由 `tasks/G5/S2-002` 覆盖 |
| 必读章节可定位 | 通过 | `.codebuddy/plans/轮41指令.md §1/§3/§4.4`、`验收报告-轮40.md §3`、`docs/11b-轮36-G6交付方案.md §4`、`CONTRACT.md §2` 均在位 |
| 上游交付物可获取 | 通过 | 轮 40 `tasks/G6/V1-001/` 首版 zip、`dist/release-summary.json`（`zip_sha_matches_build=true`）、`src/recorder/main.cpp:105-250` |
| 文件域无交集 | 通过 | 本卡仅 `src/recorder/main.cpp` + `tasks/G5/S1-009/*`；`verify/tools/build_release.ps1` 的断言由 V1-002 卡承接，目录互斥 |

## 1. `--help` 四个参数（判据）

- 验证方式：**包内 exe**（`Expand-Archive` 解压出来的文件，不是构建目录 exe）的 `--help` 原文。
- 命令：

```powershell
Expand-Archive -Path dist/ScreenRecorder-0.1.0-win-x64.zip -DestinationPath <dir> -Force
& <dir>/ScreenRecorder-0.1.0-win-x64/bin/sr-recorder.exe --help
```

- 输出（`artifacts/package-help.txt:13-17`）：

```
  --record     直接录制一段（不启动 UI），配合 --seconds/--fps/--out 使用
  --window     <hwnd> 按窗口录制，hwnd 支持十进制或 0x 十六进制；
               仅窗口源下有效，指定后忽略 --region/--display（窗口源优先）
  --fit        pad|stretch 窗口宽高比变化时的适配策略（配合 --window 使用）
  --region     x,y,w,h 按区域录制；与 --window 不可同时生效（--window 优先）
```

- 四个串 `--record` / `--window` / `--fit` / `--region` 齐全；`--window` 写清
  「仅窗口源下有效」及与 `--region`/`--display` 的优先级（轮 41 指令 §4.4）。
- 结论：**通过**。发布构建（`SR_TEST_HOOKS=OFF`）的 `--help` 不再隐藏这些参数。

## 2. 没有把调试开关带进发布 help（回归护栏）

- 命令：`pwsh -File verify/tools/build_release.ps1`
- 输出（`artifacts/build-release.txt`）：

```
PASS release-help-no-debug-switch — --help 里残留的调试开关：无（期望 0）
PASS release-help-four-params — --help 缺少的参数：无（期望 0）
PASS extract-help-no-debug — 解压后 --help 无调试开关
PASS extract-help-four-params — 解压后 --help 缺少的参数：无（期望 0）
PASS release-version-ok — --version 输出：sr-recorder 0.1.0 (... build release, test_hooks=0 ...)
RESULT: 17 PASS / 0 FAIL
```

- `debugSwitches` 六项（`--inject-drop-every` / `--fail-write-after` / `--fail-encoder` /
  `--preflight-fail` / `--fallback-dir` / `--diag-window-test`）在发布 help 里命中 0。
- 结论：**通过**。新增的四参数文本没有连带泄漏调试开关。

## 3. 包确实用当前源码重建（不是 `-SkipBuild`）

- 命令：`pwsh -File verify/tools/build_release.ps1`（未传 `-SkipBuild`）
- 输出（`artifacts/build-release.txt`）：

```
sr-recorder.exe SHA256 = 4BCEE50EE85EEB26E2FB59C10B81548FC8114EF44993AB9074BC545A475E8E12
PASS zip-recorder-sha-matches-build — 包内 sr-recorder SHA256=4BCEE50E…E12；构建目录=4BCEE50E…E12
RESULT: 17 PASS / 0 FAIL ; 包 dist/ScreenRecorder-0.1.0-win-x64.zip（341,637 字节）
```

- `dist/release-summary.json` 记 `built_now=true`、`zip_sha_matches_build=true`。
- 说明：轮 40 的 recorder SHA 是 `E61C3415…`，本轮 `4BCEE50E…`，与「改了 `PrintUsage()` 后重编译」一致；
  `sr-ui.exe` 未改，SHA 仍为 `29394B03…`（与轮 40 相同），符合预期。
- 结论：**通过**。

## 4. 改动范围（红线核对）

| 项 | 结果 |
| --- | --- |
| `ParseArgs`（`src/recorder/main.cpp:134-250`） | **未改**；四个参数的解析原本就在 `#if SR_TEST_HOOKS` 之外，发布构建一直可用 |
| `PrintUsage()` 无条件段（`:105-132`） | 只增 6 行帮助文本 |
| `#if SR_TEST_HOOKS` 调试段 | 把首行的 `--record --seconds/...` 改成 `--seconds/...（配合 --record）`，避免发布/调试 help 里 `--record` 重复；调试开关一个没删、一个没加 |
| 接口签名 | 未变（无需契约变更单） |
| `vcpkg.json` / `CONTRACT.md` | 未改 |
| `verify/` 下用例 | 未改（`build_release.ps1` 新增断言由 V1-002 卡承接） |

## 5. 红-绿对照

| 新断言 | 旧实现实测（轮 40 包） | 新实现实测（轮 41 包） |
| --- | --- | --- |
| 包内 `--help` 含 `--record`/`--window`/`--fit`/`--region` | 四个串都不在 help（验收报告-轮40 R4；功能在、help 未列） | 四个串齐全，`artifacts/package-help.txt:13-17` |
| `release-help-four-params` | 断言不存在（轮 40 脚本只查调试开关） | `PASS`，缺 0 |
| `extract-help-four-params` | 断言不存在 | `PASS`，缺 0（在解压产物上验） |

> 红证据不是构造的：验收方轮 40 独立重跑时已记录「`--help` 没列出这些参数（R4）」，
> 且 `build-msvc` 旧 help 的 `--record` 只在 `SR_TEST_HOOKS=ON` 调试段里，
> 发布构建 `--help` 见不到（可由 `git`-less 的旧包 `release-summary` 与本报告核对）。

## 6. 已知限制

1. `--window` 的适用条件是按 `src/recorder/RecordingSession.cpp:180-185` 的
   `window_handle != 0 → CaptureKind::Window`（窗口源优先）写的，属读码结论；
   窗口源下的实际行为证据见 `tasks/G5/S2-002/EVIDENCE.md`（`g5_window` 24 断言）。
2. 本卡只补可发现性，未新增功能；M1-08 的功能交付归 `S2-002`。
3. 执行方不自评为已验收，结论待验收方独立重跑。
