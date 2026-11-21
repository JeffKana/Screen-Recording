# EVIDENCE · V1-025（轮 53 · 0.1 MVP 发布收尾）

> 本轮**零 `src/` 改动**（`git diff origin/main..HEAD -- src/` 只含轮 52 的 faststart 变更，本轮自身为 0）。
> 所有证据都是「改前 → 改后」或「可重跑命令 + 原始输出」。

---

## §1 P0-1 文档回写 0.1 口径

### 1.1 逐文件改了什么

| 文件 | 改前 | 改后 |
| --- | --- | --- |
| `docs/22` | ⑤⑥ 归属「轮 53（矩阵补格 + 发布判定）」「轮 53 / 真机触发」；4 条真机归属「轮 53 / 真机触发」；① 行无例外指向 | ⑤⑥ 与 4 条真机 → **「1.0 前置 / 0.1 例外」**；① 行末尾 → **「0.1 例外共 5 条（M6-01#1/M6-04#1/M6-05#1/M6-08#1/M1-27#1）逐条见 `docs/27 §3`（EX-02~EX-06）」**；另加「2026-11-21 发布时点回写」头注与 §3 的「0.1 可发布？是（已发布）」。旧措辞「v0.1 范围按 O0 例外收窄」已替换 |
| `docs/09` | 4 条「**BLOCKED（缺机型 / 缺 2 台真机 / 缺 3 台机器 / 缺混合显卡真机）**」；`M1-27#1`「已登记为发布例外，见 `docs/05 §7.2`」 | 4 条 → **「0.1 范围外（`docs/27 §3` EX-02/03/04/05，1.0 前置）」**；`M1-27#1` → **「0.1 范围外（`docs/27 §3` EX-06，1.0 前置）」**。**状态列一字未改**（仍 `无卡`×4 / `有卡未实现`×1） |
| `docs/23` | `## 4. 转 GA 的硬条件（5 条，缺一不可）` | `## 4. 转 1.0-GA 的五条硬条件（0.1 不走这套，见 docs/27 §4）` + 一段区分说明（0.1 用 `docs/27 §4` 六条，两套不许混用） |
| `docs/24` | rc.1 行无备注；rc.2 预留注记 | rc.1 行补 **「已被 0.1 取代」**（并引 `docs/27 §2`）；新增 **0.1.0 行**（标签/提交/包名/大小/SHA/日期/说明）；「rc.2 预留」段改为「不再发布，0.1 之后的补丁走 `0.1.1`，不改已有 tag」；新增「0.1 的发布依据 = `docs/27 §4`」；校验方式段改成 0.1.0 的 zip/exe SHA |
| `README` | 横幅「当前版本 `0.1.0-rc.1`（V1.0 候选版，内部使用）」 | 横幅 **「当前版本 `0.1`（MVP，仅本机验证）」** + 「只在 Win11 + Intel 核显 + 单屏 100% 验证；其它机型未测」 + **「兼容性范围与已知限制见 `RELEASE-NOTES-0.1.0.md`」** + 「1.0-GA 的门槛见 `docs/23 §4`（与 0.1 不是一套）」；安装段标题改「0.1 期」、包名改 `ScreenRecorder-0.1.0-win-x64.zip` |
| `RELEASE-NOTES-0.1.0.md` | 已知限制第 4 条「MP4 收尾 ≈0.5 s/GB」；长稳行「8h×2、静音段×2」 | 第 4 条改为「已改预留+回填（1.1GB 实测 0.98ms）；**实际录制时长明显超过 `--seconds` 预估时**退化为整体搬移（≈0.5 s/GB）」；长稳行改「8h ×2（第 3 次进行中，**不计入本版声明**）、静音段 ×3」 |

### 1.2 判据：`coverage_report --check` 仍 PASS（改的是理由不是状态）

```powershell
python verify/tools/coverage_report.py --check
# coverage table is up to date

python verify/tools/contract_check.py
# 汇总: 8 项检查, 8 PASS, 0 FAIL
```

| 项 | 结果 |
| --- | --- |
| 覆盖状态 | **111 / 1 / 4**（与轮 51/52 一致，未翻绿、未回退） |
| `docs/09` 生成物与理由列 | `--check` PASS（说明「手工维护的理由列」与生成结果一致） |

### 1.3 判据：`docs/27` 被至少 3 个文件引用

```powershell
Select-String -Path docs/*.md,README.md,RELEASE-NOTES-0.1.0.md -Pattern "docs/27|27-0\.1范围"
# 26 处，分布在 8 个文件
```

| 引用方 | 用途 |
| --- | --- |
| `docs/22` | ① 行的 5 条例外 + ⑤⑥/真机的归属 |
| `docs/09` | 4 条 + `M1-27#1` 的挂账理由 |
| `docs/23` | §4 标题里指向「0.1 走 docs/27 §4」 |
| `docs/24` | 0.1.0 行的例外说明 + 「rc 已被 0.1 取代」 |
| `README` / `RELEASE-NOTES-0.1.0.md` / `docs/INDEX.md` / `docs/07` | 顶部指向 / 记录 |

> 旧措辞检查：`rg "缺机器待排期|BLOCKED（缺"` 在 `docs/09` 中已无命中（4 条 + `M1-27#1` 全部改口径）。

---

## §2 P0-2 出 0.1.0 包

```powershell
pwsh -File verify/tools/build_release.ps1 -Version 0.1.0
# 原始输出：verify/artifacts/r53-build_release-0.1.0.txt
```

### 2.1 `channel` 三态（`build_release.ps1`）

| 版本字符串 | channel | ga_ready |
| --- | --- | --- |
| 含 `-rc.`（如 `0.1.0-rc.2`） | `rc` | `0` |
| 等于 `0.1.0` | **`mvp`** | **`0`** |
| 其余（如 `1.0.0`） | `ga` | `1` |

### 2.2 结果

| 项 | 值 |
| --- | --- |
| 自检 | **17 PASS / 0 FAIL**（`RESULT: 17 PASS / 0 FAIL`） |
| 包 | `dist/ScreenRecorder-0.1.0-win-x64.zip`，**375,574 B** |
| zip SHA256 | **`23B9E7D9964CDFA98B55D44652565558C7AA8EE52401B0776B06D7A6149692DE`** |
| 包内 `VERSION.txt` | `name=ScreenRecorder` / `version=0.1.0` / **`channel=mvp`** / **`ga_ready=0`** / `build_flags=SR_TEST_HOOKS=OFF;SR_BUILD_TOOLS=OFF` / `build_sha=5609589` |
| 包内 exe | `sr-recorder.exe E1C59EC2…66FE83`、`sr-ui.exe 1CD57F09…A9DB21` |
| 与构建产物一致 | `zip_sha_matches_build=true`（脚本从**解压后的文件**重算比对） |
| `source_fingerprint` | `FFD3DF70AF6A7DC146E3E3A319CAB536C69D9E39BE3E506716FCAD9DA31F75C1` |
| 交付包不含调试开关 | `release-help-no-debug-switch` PASS（`--mp4-reserve` 只在 `SR_TEST_HOOKS=ON` 构建里，包内无） |

---

## §3 P0-3 干净目录冒烟（发布前最后一关）

全流程日志：`verify/artifacts/r53-smoke-0.1.0.txt`

```powershell
# 1) 干净目录解压
$smoke = "$env:TEMP\r53-smoke-133529"; Expand-Archive dist/ScreenRecorder-0.1.0-win-x64.zip -DestinationPath $smoke
# 2) 安装（默认目录 %LOCALAPPDATA%\ScreenRecording）
pwsh -File "$smoke\ScreenRecorder-0.1.0-win-x64\install.ps1"
# 3) 用**安装后的** exe 录 60s（默认参数，不指定 --fps/--bitrate）
& "$env:LOCALAPPDATA\ScreenRecording\bin\sr-recorder.exe" --record --seconds 60
# 4) 卸载
pwsh -File "$smoke\ScreenRecorder-0.1.0-win-x64\uninstall.ps1"
```

| 判据 | 实测 | 结论 |
| --- | --- | --- |
| ① 成片可播 | `out.mkv` 46,498,959 B；`ffprobe duration=59.999`；`ffmpeg -v error -i out.mkv -f null -` → **error 行 0** | PASS |
| ② 卸载后无残留目录 | `install_dir_exists=False`；`registry_exists=False`（HKCU 卸载项已清）；`已删除程序目录 …\ScreenRecording`；`已移除开始菜单快捷方式` | PASS |
| ③ 用户录制文件未被删 | 预置 `%USERPROFILE%\Videos\ScreenRecording\user-keep-test.mkv` / `.txt`：卸载前 `62,62` → 卸载后 `62,62`；卸载脚本自己打印「保留用户成片：…（要一并删除请加 `-Purge`）」 | PASS |

> 一次失败的取证也留着：第一次「预置文件」写成了 `[byte[]](1..1024)`（>255 溢出），文件没建成 →
> 第三次判据其实没测到；已在同一份日志里**重做**（`=== 7) 复测 ===`），上面的 `before/after` 来自重做那次。

---

## §4 P0-4 发布

```powershell
git tag -a v0.1.0 -m "0.1（MVP，仅本机验证）…"
git push origin v0.1.0
gh release create v0.1.0 dist/ScreenRecorder-0.1.0-win-x64.zip `
    --title "0.1（MVP，仅本机验证）" --notes-file RELEASE-NOTES-0.1.0.md    # 不加 --prerelease
gh release view v0.1.0 --json tagName,name,isPrerelease,isDraft,assets
```

| 项 | 实测 |
| --- | --- |
| 地址 | `https://github.com/JeffKana/Screen-Recording/releases/tag/v0.1.0` |
| 标题 | `0.1（MVP，仅本机验证）` |
| `isPrerelease` | **false**（0.1 是正式交付，不是 rc） |
| `isDraft` | false |
| 资产 | `ScreenRecorder-0.1.0-win-x64.zip`，`size=375574`，`digest=sha256:23b9e7d9964cdfa98b55d44652565558c7aa8ee52401b0776b06d7a6149692de`（与本地一致） |
| tag 目标 | `v0.1.0 → 53e6c61`（轮 53 的卡提交）；包由 `5609589` 构建，两者 `src` 指纹相同（`FFD3DF70…`），`VERSION.txt` 的 `build_sha` 记的是后者 |
| `docs/24` | 已补 0.1.0 行（版本/类型/标签/提交/包名/大小/SHA/日期/说明 + release 链接） |
| `docs/22` | 「当前」列已回写发布时点（头注 + §3 的「0.1 可发布？是」） |

> 本版**没有**覆盖任何已发布资产：rc.1 的 zip 与 tag 原样保留；0.1 一旦推出，后续修复走 `0.1.1`，不改 `v0.1.0`。
