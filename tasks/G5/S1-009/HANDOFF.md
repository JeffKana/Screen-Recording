# HANDOFF: S1-009（轮 41 · 发布包 `--help` 补全）

## 一、我改了什么接口？

**没有接口变更**，`CONTRACT.md` / `vcpkg.json` 均未动。

| 改动 | 性质 |
| --- | --- |
| `src/recorder/main.cpp:105-132`（`PrintUsage()` 无条件段） | 增加 6 行帮助文本：`--record` / `--window`（含适用条件与优先级）/ `--fit` / `--region` |
| `src/recorder/main.cpp` 的 `#if SR_TEST_HOOKS` 段 | 首行由 `--record --seconds/...` 改为 `--seconds/...（配合 --record）`，避免 hooks 构建里 `--record` 重复出现；调试开关一个没删、一个没加 |
| `ParseArgs`（`:134-250`） | **未改**——四个参数的解析本来就在条件编译之外，发布构建一直可用 |
| `verify/tools/build_release.ps1` | 由 V1-002 卡新增两条 help 断言（本卡不改 `verify/`） |
| `tasks/G5/S1-009/*` | 本卡四件套 |

## 二、下游需要接什么？

1. 验收方独立重跑：`pwsh -File verify/tools/build_release.ps1`（17/17），
   再 `Expand-Archive dist/ScreenRecorder-0.1.0-win-x64.zip` 后跑包内
   `bin\sr-recorder.exe --help` 核对四个串与 `--window` 的适用条件。
2. 若后续再加 CLI 参数，需同步 `PrintUsage()` 与 `build_release.ps1` 的
   `$requiredHelpParams` 数组，避免再次出现「功能在、help 没列」。
3. `dist/` 的 zip 与 `release-summary.json` 已用当前源码重建（未用 `-SkipBuild`），
   `sr-recorder.exe` SHA 由轮 40 的 `E61C3415…` 变为 `4BCEE50E…`；`sr-ui.exe` 未改，
   SHA 仍 `29394B03…`。

## 三、已知限制是什么？

1. **本卡只补可发现性，不新增功能**。验收方轮 40 已证伪「包陈旧」假设：
   包内 exe 实测 `--record` 能录，二进制里 `--window/--fit/--record` 串都在。
2. `--window`「仅窗口源下有效、指定后忽略 `--region`/`--display`（窗口源优先）」
   是按 `src/recorder/RecordingSession.cpp:180-185` 的读码结论写的；
   窗口源下的实际行为证据见 `tasks/G5/S2-002/EVIDENCE.md`（`g5_window` 24 断言）。
3. `--fit` 说明为「配合 `--window` 使用」：`SetFitMode` 在会话里对转换器统一设置，
   但可见效果只在窗口宽高比变化时出现；help 未声称全场景生效。
4. 调试段首行措辞改动只在 `SR_TEST_HOOKS=ON` 的构建可见，发布包不含该段。

## 四、哪些需求我**没**做完？

| 项 | 状态 | 原因 / 下一步 |
| --- | --- | --- |
| M1-08 / M1-09 的功能实现 | 不在本卡 | 由 `tasks/G5/S2-002` 交付，本卡只补 help 文本 |
| `sr-ui.exe` 的 `--help` 补全 | **没做** | 判据只要求「包内 exe 的 help 含四个参数」，这四个参数属于 `sr-recorder`；未扩大范围 |
| 包内 `--help` 的「已验收」 | **不自评** | 待验收方独立重跑 |
| 三构建零告警 | 由主卡回归验证 | 见 `tasks/G6/V1-002/artifacts/regression.txt` |

## 五、交付包清单

```
tasks/G5/S1-009/  TASK.md EVIDENCE.md SELFTEST.md HANDOFF.md
证据：tasks/G6/V1-002/artifacts/package-help.txt、build-release.txt
```

**执行方不自评为已验收。**
