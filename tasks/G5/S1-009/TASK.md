# TASK: S1-009 轮 41 · 发布包 `--help` 补全（R4，CLI 可发现性）

阶段：G5｜角色：S1（recorder 进程骨架）｜轮次：41｜来源：验收方验收报告-轮40（R4）+ `.codebuddy/plans/轮41指令.md` §1 P1

> 轮 41 无预置卡，执行方按验收方指令建卡（同轮 40 S5-004 做法）。
> **勘误（验收方轮 40 报告 §3）**：曾怀疑「包是陈旧的、里面没有 M1-08」，直接测包内 exe 后推翻：
> `--record` 实际能录（`record_exit=0`），二进制里 `--window/--fit/--record` 三个串都在，包是新的。
> 唯一问题就是 **`--help` 没列出这些参数**（用户不可发现）。本卡只修帮助文本，不动解析逻辑。

## 需求范围

| 项 | 判据（轮 41 指令 §1 / §3） |
| --- | --- |
| 发布包 `--help` 列出 `--record` / `--window` / `--fit` / `--region` | 包内 exe 的 help 含这四个串；`build_release.ps1` 自带该断言 |
| `--window` 适用条件写清 | help 里写明只在窗口源下有效，避免用户误用（轮 41 指令 §4.4） |

## 必读章节

- `.codebuddy/plans/轮41指令.md` §1、§3、§4.4
- `.codebuddy/plans/验收报告-轮40.md` §3（发布包新旧判别被推翻的记录）
- `docs/11b-轮36-G6交付方案.md` §4（发布构建开关 `SR_TEST_HOOKS=OFF`）
- `CONTRACT.md` §2（文件域归属）

## 文件域（白名单）

- `src/recorder/main.cpp`（`PrintUsage()` 主体；`ParseArgs` 一行不动）
- `tasks/G5/S1-009/*`

> `verify/tools/build_release.ps1` 的「包内 help 四参数」断言由 `tasks/G6/V1-002` 承接（V1 域），
> 本卡不越界改 `verify/`。两卡目录互斥，无交集。

## 上游交付

- 轮 40 `tasks/G6/V1-001/`（G6 首版 zip）+ `dist/release-summary.json`（包内/构建目录 exe SHA256 相同）
- `src/recorder/main.cpp:105-132`（`PrintUsage`）、`:134-250`（`ParseArgs`，四个参数均非 `SR_TEST_HOOKS` 条件编译）

## 约束

1. **不引三方依赖**（`vcpkg.json` 只读）、不改 `CONTRACT.md`、不动接口签名（无契约变更单需求）。
2. **只改帮助文本**：不得改解析逻辑、不得把调试开关列进发布 help（`build_release.ps1` 有
   `release-help-no-debug-switch` 断言，列了 6 个调试开关，漏一个就 FAIL）。
3. 不声称「包内功能此前缺失」——验收方已实测证明功能在，只缺可发现性。
4. 不自评已验收；构建与包内验证由验收方独立重跑。

## 完成定义（DoD）

- [ ] `PrintUsage()` 无条件段列出四个参数，`--window` 带适用条件说明
- [ ] `SR_TEST_HOOKS=OFF` 的 `--help` 含四个串，且不含 6 个调试开关
- [ ] `build-msvc`（含 hooks）与 `build-release`（不含 hooks）均零告警编译通过
- [ ] 重打包后包内 `sr-recorder.exe --help` 的四参数断言通过、`zip_sha_matches_build=true`
- [ ] `g5_storage` / `g5_window` / `g5_uac` 等录制类回归零失败

## 覆盖子句

无（CLI 可发现性修复，不新增/不改写需求子句；上游 M1-07/M1-08/M1-09 已由 `S2-002` 覆盖）。

## 交付状态

- 发布包 `--help` 四参数 + `--window` 适用条件：已实现（待验收方核对包内 exe）
