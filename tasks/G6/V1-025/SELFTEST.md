# SELFTEST · V1-025（轮 53）

## §0 开工四项自查（`docs/06 §4.2`）

| 项 | 结论 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | **通过** | 本轮**不加功能**，落的是 `docs/27`（0.1 范围冻结，O0 2026-11-20 拍板）与 `docs/05 §7.2` 的判据归属；覆盖表状态不变（111/1/4），无新增需求号 |
| 必读章节可定位 | **通过** | `docs/27 §2/§3/§4/§6`、`docs/23 §4/§5/§7`、`docs/05 §7.1/§7.2`、`docs/24` 检查清单、`.codebuddy/plans/轮53指令.md`、`RELEASE-NOTES-0.1.0.md` 均存在且编号可定位 |
| 上游交付物可获取 | **通过** | `round/52`（faststart 预留回填 + LT-01 第 3 次启动）；本分支基于 `round/52`，两者都待 O0 按 SOP 合并 |
| 文件域无交集 | **通过** | 只碰 `docs/{07,09,22,23,24}`、`README.md`、`RELEASE-NOTES-0.1.0.md`、`verify/tools/build_release.ps1`（channel 三态）、`tasks/G6/V1-025/**`；`src/**` 零改动 |

> **顺序说明（已知偏差）**：`docs/20 §6` 规定「合并权在 O0」，而轮 53 卡要求「轮 52 合并后再开」。
> 本轮是在 `round/52` 之上**叠分支**完成的（`round/53` 基于 `round/52`，不是从 `main` 拉），
> 合并顺序必须是 `round/52` → `round/53`；这一条写在 `HANDOFF.md §4`，不是自评通过。

## §1 本轮改动清单（越界自查）

| 文件 | 性质 |
| --- | --- |
| `docs/22` | ⑤⑥/4 条真机归属改「1.0 前置 / 0.1 例外」；① 行列 5 条例外；发布时点头注；§3 结论两行 |
| `docs/09` | 4 条 + `M1-27#1` 的挂账理由改「0.1 范围外（docs/27 §3）」；**状态列未改** |
| `docs/23` | §4 标题 + 0.1/1.0 两套门槛的区分说明 |
| `docs/24` | rc.1 行「已被 0.1 取代」+ 0.1.0 行 + 校验方式 + 0.1 发布依据 |
| `README.md` | 横幅 0.1 + 兼容性范围指向 + 安装段包名 |
| `RELEASE-NOTES-0.1.0.md` | 已知限制第 4 条按 faststart 实况更新；长稳行改「8h×2 + 第 3 次进行中」 |
| `verify/tools/build_release.ps1` | `channel`/`ga_ready` 三态（只影响 `VERSION.txt` 两个字段） |
| `docs/07` | 轮 53 行（留白）+「轮 53 记录」 |
| `tasks/G6/V1-025/**` | 四件套 |

**未改**：`src/**`、`docs/01`、`docs/05`、`docs/27`（裁定原文）、`verify/cases/**`、`vcpkg.json`、默认容器。

## §2 静态检查

```powershell
python verify/tools/contract_check.py          # 汇总: 8 项检查, 8 PASS, 0 FAIL
python verify/tools/coverage_report.py --check # coverage table is up to date（111 / 1 / 4）
python verify/tools/card_consistency_check.py  # OK —— 0 处冲突
python verify/tools/domain_table_check.py      # MISMATCH_COUNT=0
```

## §3 出包（17 项自检）

```powershell
pwsh -File verify/tools/build_release.ps1 -Version 0.1.0
# RESULT: 17 PASS / 0 FAIL ; 包 dist\ScreenRecorder-0.1.0-win-x64.zip（375,574 字节）
```

`VERSION.txt` 三态核验：

```powershell
Get-Content "<解压目录>\ScreenRecorder-0.1.0-win-x64\VERSION.txt"
# version=0.1.0 / channel=mvp / ga_ready=0
```

## §4 冒烟与发布

| 步 | 命令 | 结果 |
| --- | --- | --- |
| 解压 | `Expand-Archive dist/ScreenRecorder-0.1.0-win-x64.zip -DestinationPath $smoke` | 包内 `bin/ + install.ps1 + uninstall.ps1 + README.txt + VERSION.txt` |
| 安装 | `pwsh -File $pkg\install.ps1` | `已安装到 %LOCALAPPDATA%\ScreenRecording`、开始菜单快捷方式、卸载登记 |
| 录 60s | `& "$env:LOCALAPPDATA\ScreenRecording\bin\sr-recorder.exe" --record --seconds 60` | `out.mkv` 46,498,959B、`duration=59.999`、解码 0 error |
| 卸载 | `pwsh -File $pkg\uninstall.ps1` | 程序目录已删、卸载登记已删、**用户成片保留**（`62B → 62B`） |
| 发布 | `gh release create v0.1.0 …` | `isPrerelease=false`、资产 375,574B、`digest=sha256:23b9e7d9…` |

## §5 Definition of Done

- [x] 任务卡每条需求都有对应证据（`EVIDENCE.md §1~§4`）
- [x] 本轮无新增断言（改的是文档与打包脚本的一个字段）→ 无「先红后绿」项；`channel` 三态用**实际出包**验证（rc.1 曾是 `rc/0`，本轮 `mvp/0`）
- [x] 三构建零告警（`src/` 未改；`build-msvc` / `build-mingw` 在前一轮已验证，本轮只重跑 `build-release`）
- [x] 回归零失败（沿用轮 52 的用例结果；本轮代码零改动）
- [x] 无死代码 / 无调试残留
- [x] 改动未超出文件域（`§1`）
- [x] 接口无变更（`CONTRACT.md` 未动）
- [x] 文档已同步（`docs/07/09/22/23/24` + `README` + `RELEASE-NOTES`）
- [x] 发布物未被覆盖（rc.1 tag/zip 原样；`v0.1.0` 首次推出）
- [ ] **例外（EX-02~06）一条未测** → 发布说明里只写「未测」，不计为通过
