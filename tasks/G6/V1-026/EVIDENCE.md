# EVIDENCE · V1-026（轮 54 · `R1-52` 原子占位 + 0.1.1 + 长稳收口）

---

## §1 P0-1 `R1-52`：同名 `.part` 的原子占位

### 1.1 修的是什么（TOCTOU）

```text
0.1.0：  exists(x.part)?  →  false  →  fopen(x.part,"wb+")     ← 查与用之间是可抢占的
         A: exists=false ─┐
         B: exists=false ─┴→ 两边都 fopen 同一个 x.part → 写入交错
0.1.1：  CreateFileW(x.part, CREATE_NEW) ── 成功即「选名 + 占用」一步完成
         A: 成功 → 拿到句柄 → 交给写入器
         B: ERROR_FILE_EXISTS → 试 x.1.part → 成功（顺延）
```

**没只加锁**：改的是选名本身（原子创建），不是「选完再加锁」。
句柄经 `MkvWriter::AdoptOpen` / `Mp4Writer::AdoptOpen` **直接交给写入器**，
中间不再有第二次 `fopen`（否则等于把 TOCTOU 又请回来一半）。

| 文件 | 改动 |
| --- | --- |
| `src/recorder/OutputPath.h/.cpp` | `OutputPlan::part_file` + `CloseReservedPart`；候选名逐个 `CREATE_NEW`，`ERROR_FILE_EXISTS` → 下一个 `.part` 变体 → 下一个 `_NNN` 名；非并发错误（权限/磁盘满）直接拒绝（`M8-06`） |
| `src/mux/MkvWriter.h/.cpp`、`src/mux/Mp4Writer.h/.cpp` | `AdoptOpen(FILE*, path, error)`：接管已有句柄，`Open()` 变成「`fopen` + `AdoptOpen`」；所有权统一由写入器关闭 |
| `src/recorder/RecordingSession.cpp` | 句柄移交 + `output reserved: <part>` 日志（撞车的可核证据）；`--wait-for-part` 屏障（仅 `SR_TEST_HOOKS`） |
| `src/recorder/main.cpp` | 解析 `--wait-for-part`（仅 `SR_TEST_HOOKS`，交付包的 `--help` 不含它） |

### 1.2 绿端（本轮构建，20 轮 ×2 进程，屏障注入）

```powershell
pwsh -File verify/cases/g3_output.ps1
# 原始输出：verify/artifacts/r54-g3_output-green-hooksbuild.txt
```

```text
PASS r54-concurrent-no-corruption — 并发 20 轮 × 2 进程（屏障=True）：产物 40 个 / 损坏 0 个 / 撞车轮数 20
PASS r54-concurrent-stable-outcome — exit≠0 次数=0；互不覆盖的不同文件名 40 个（期望 40）；.part 残留 0 个
RESULT: 17 PASS / 0 FAIL
```

- **撞车轮数 20/20**：每一轮两个进程都拿到了**不同的** `.part`（`X.part` / `X.1.part`，
  见新日志 `output reserved=`），说明 20 次都是真并发抢同一路径，不是「没撞上所以没坏」。
- 40 个产物全部 `mkv_probe` + `ffmpeg -f null` 干净；收尾后 0 个 `.part` 残留。

### 1.3 红端（同一份用例，被测对象换成 0.1.0 的 exe）

```powershell
$old = "$env:TEMP\r54-old010\ScreenRecorder-0.1.0-win-x64\bin\sr-recorder.exe"   # 从 dist 的 0.1.0 包解出
pwsh -File verify/cases/g3_output.ps1 -RecorderPath $old
# 原始输出：verify/artifacts/r54-g3_output-red-0.1.0.txt
```

```text
FAIL r54-concurrent-no-corruption — 并发 20 轮 × 2 进程（屏障=False）：产物 38 个 / 损坏 7 个
FAIL r54-concurrent-stable-outcome — exit≠0 次数=2；互不覆盖的不同文件名 38 个（期望 40）；.part 残留 0 个
RESULT: 15 PASS / 2 FAIL
```

- A/B/C/D1/D2 五段在旧 exe 上仍然全过 → 这 2 条 FAIL 是**新增判据**打出来的，不是既有用例被放宽/改坏。
- 另一次红端（同一 exe，上一轮同脚本）为 39 产物 / **11 损坏**：同一缺陷的两次独立复现。

### 1.4 D-32 残留避让未被破坏

`g3_output` 的 `stale-part-untouched` / `residue-avoided-name` 在绿端仍是 PASS：
预置 16 字节的 `res.mkv.part` → 新录制落在 `res.mkv.1.part`，成品名仍是 `res.mkv`（**行为与 0.1.0 一致**）；
`CREATE_NEW` 只是把「查存在」换成「创建失败即已存在」，命名规则一字未改。

---

## §2 P0-2 0.1.1 出包

```powershell
pwsh -File verify/tools/build_release.ps1 -Version 0.1.1
# 原始输出：verify/artifacts/r54-build_release-0.1.1.txt
```

| 项 | 值 |
| --- | --- |
| 自检 | **17 PASS / 0 FAIL** |
| 包 | `dist/ScreenRecorder-0.1.1-win-x64.zip`，**377,450 B** |
| zip SHA256 | **`ADADE5A34095DF1ACBECE4ADADC744E326B5B2D15A019E0D662E35CBAC909766`** |
| 包内 `VERSION.txt` | `version=0.1.1` / **`channel=mvp-patch`** / **`ga_ready=0`** / `build_sha=627732f` / `SR_TEST_HOOKS=OFF;SR_BUILD_TOOLS=OFF` |
| 包内 exe | `sr-recorder.exe 3603DABD…1277`（含修复）、`sr-ui.exe 1CD57F09…DB21`（与 0.1.0 相同：UI 未改） |
| 与构建产物一致 | `zip_sha_matches_build=true`（脚本从解压后的文件重算比对） |
| `source_fingerprint` | `CAAAFB0CF107697265DAC4B77656A6BF5027852FB46FCB8259F39FF98A7AFCF9` |
| 交付包不含调试开关 | `release-help-no-debug-switch` PASS（`--wait-for-part` 只在 `SR_TEST_HOOKS=ON` 的构建里） |

`channel` 四态（`build_release.ps1`）：

| 版本 | channel | ga_ready |
| --- | --- | --- |
| 含 `-rc.` | `rc` | 0 |
| `0.1.0` | `mvp` | 0 |
| `0.1.1` / `0.1.2` … | **`mvp-patch`** | 0 |
| 其余（如 `1.0.0`） | `ga` | 1 |

> 用 0.1.1 的**发布构建**（`build-release`）再跑一遍 `g3_output`：**17 PASS / 0 FAIL**
> （`verify/artifacts/r54-g3_output-green-0.1.1.txt`；该构建无屏障开关，40 产物 0 损坏）。

---

## §3 P0-3 LT-01 第 3 次判定

```powershell
Get-Content verify/artifacts/lt01-real-20261121-133707/summary.json -Raw   # 2026-11-21 13:37:07 启动，21:37 收尾
```

`recorder_sha256 = 2026BCCC…1575`（0.1.0 线的 `build-msvc`），`aborted=false`，`record_exit=0`，`pass=10 / fail=0`。

| # | 判据（`g5_lt01_real.ps1`） | 实测 | 结论 |
| --- | --- | --- | --- |
| 1 | 进程正常退出 | `exit=0`、`span_s=28800.4` | PASS |
| 2 | 成片时长 ≥ 28,790s | **28,799.971s** | PASS |
| 3 | 媒体时间轴 ≥ 28,790s | `audio_last_pts=28,799.988s`、`video_last_pts=28,799.971s` | PASS |
| 4 | 全片可解（解码 + framecrc + DTS） | `error_lines=0`（6,667 条 `benign`）、`video_frames=864,000 = frames_encoded`、`audio_frames=1,240,313`、`dts_violations=0` | PASS |
| 5 | 内存：10min WS 基线 → 峰值 ≤ +50MB 且无逐小时上行 | 基线 **558.0MB** → 峰值 **565.9MB（Δ7.9MB）**；逐小时 545.6/555.4/557.4/557.9×5（第 3 小时后持平） | PASS |
| 6 | 句柄 ≤ +10% | 488 → **496（+1.64%）** | PASS |
| — | 丢帧率 < 0.1% | `dropped=0` / `captured=214,273` | PASS |
| — | 16 点漂移 ≤ 100ms | **最差 26ms @ 25,200s（7h 点，音频领先）**；收尾点 17ms | PASS |

> 末段音频补齐：`audio_tail_fill_frames=5,046`（其中静音填充 4,826），属设计内的尾段对齐，不影响可播与漂移口径。
> **判据 ③ 结论：LT-01 3/3、LT-02 3/3 → 3/3 达成**（`docs/22` 已回写）。

---

## §4 P0-2 发布

```powershell
git tag -a v0.1.1 -m "0.1.1（MVP 补丁，仅本机验证）：只修 R1-52"
git push origin v0.1.1
gh release create v0.1.1 dist/ScreenRecorder-0.1.1-win-x64.zip `
    --title "0.1.1（MVP 补丁，仅本机验证）" --notes-file RELEASE-NOTES-0.1.1.md
gh release view v0.1.1 --json tagName,name,isPrerelease,isDraft,assets
```

| 项 | 实测 |
| --- | --- |
| 地址 | `https://github.com/JeffKana/Screen-Recording/releases/tag/v0.1.1` |
| 标题 | `0.1.1（MVP 补丁，仅本机验证）` |
| `isPrerelease` / `isDraft` | **false / false**（补丁版是正式交付；不加 `--prerelease`） |
| 资产 | `ScreenRecorder-0.1.1-win-x64.zip` / `size=377450` / `digest=sha256:adade5a3…766`（与本地一致） |
| tag 目标 | `v0.1.1 → 627732f`，**与包内 `build_sha` 完全一致**（0.1.0 那次的「tag 与 build_sha 差一个 docs 提交」在这里不再出现） |
| 不复用 0.1.0 资产 | `v0.1.0` 的 tag 与 zip 原样保留；0.1.1 是**新增** release，不覆盖、不改已有 tag |

---

## §5 P1 文档

| 文件 | 改动 |
| --- | --- |
| `docs/20 §1` | 新增「串行依赖」段：后一轮依赖前一轮产出时**必须等合并后从 main 拉或 rebase**，不许叠分支；轮 53 的叠分支登记为一次性例外 |
| `docs/26` | 汇总表：严重 16→17 / 闭环 13→14，去掉「待定级项」行，新增「本轮新闭环 1」；§2 严重表新增 `R1-52` 行（闭环）；§3.2 补定级/修复/验证三行 |
| `docs/22` ③ | LT-01 2/3 → **3/3**，六项数字与最差点进表 |
| `docs/24` | 新增 0.1.1 行 + 0.1.1 校验块（zip/exe/fingerprint/VERSION.txt） |
| `README` | 横幅 0.1 → **0.1.1**（MVP 补丁）+ 指向 `RELEASE-NOTES-0.1.1.md`；包名改 0.1.1 |
| `RELEASE-NOTES-0.1.1.md` | 新建：只修一条的差异表、兼容性范围（同 0.1.0）、已知限制继承、升级方式 |
| `docs/07` | 轮 54 行（留白）+「轮 54 记录」 |
