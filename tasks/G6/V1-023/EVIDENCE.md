# EVIDENCE · V1-023（轮 51 · rc 之后的发布复核与 GA 前置）

> 验收凭据：每条一项，全部是**可重跑命令 + 实际输出行**。
> `artifacts/` 不入库（`.gitignore` 已忽略 `tasks/**/artifacts/`），所以原始输出按行贴出。
> 环境：Windows 11 build 26200 / i5-13500 / UHD 770 / 单机；`D:` 初始可用 84.5GB。
> **本轮 `src/` 零改动**：`git diff ed8767a..HEAD -- src/` 为空，二进制即 rc.1 的 dev+test-hooks 构建。

---

## §0 构建与指纹（ER / LT / 收尾实测全部在同一个 exe 上）

```powershell
git rev-parse --short HEAD
Get-FileHash build-msvc/bin/Release/sr-recorder.exe -Algorithm SHA256
Get-FileHash build-msvc/bin/Release/sr-ui.exe       -Algorithm SHA256
```

```
4b117cf
recorder BAF0A783917623B3BDD717CC07238C420183A5F50E1929621DA49FD4F27AA26C
ui       26FED71BD7CDD5EC42133FBA8805C1901DB1CDFE43CF2CF8F07AD974B665230D
```

- 构建时间 2026-11-18 13:56:31（MSVC VS17 x64 / Release / `SR_TEST_HOOKS=ON`；
  ER/LT 需要 `--seconds`/`--pause-at` 这类测试钩子，发布包是 `SR_TEST_HOOKS=OFF` 的另一份产物）。
- **ER-04 / ER-02 / LT-02 / §5 的收尾实测都用上面这一份**；LT-01 用它的副本
  `sr-recorder-lt01.exe`（同一次 `Copy-Item`，逐字节相同 —— 见 `verify/artifacts/lt01-real-*/`）。

---

## §1 ER-04 磁盘耗尽 ×3（判据：`disk_stop=1`、可播、无 `.part`、无 0 字节）

### 1.1 用例口径（`g5_inject.ps1`）

```powershell
pwsh -File verify/cases/g5_inject.ps1    # ER-04 段：每轮独立文件 + 重算剩余空间阈值
```

```
--- ER-04 磁盘耗尽 ×3 ---
  PASS disk-run1 — stop=1 code=0 size=3336062 part=False
  PASS disk-run2 — stop=1 code=0 size=3665412 part=False
  PASS disk-run3 — stop=1 code=0 size=3327289 part=False
  PASS disk-stable-3x — 3 次结果: stop=1,code=0,part=False,playable=True | （同） | （同）
```

### 1.2 补充：三次**独立目录**（轮 51 卡明确要求"独立目录"）

```powershell
1..3 | % { $dir="tasks/G6/V1-023/artifacts/er04/run$_"; New-Item -ItemType Directory -Force $dir | Out-Null
  $f="$dir/er04_$_.mkv"; $free=(Get-PSDrive D).Free; $stopMb=[math]::Floor(($free-2MB)/1MB)
  & .\build-msvc\bin\Release\sr-recorder.exe --record --seconds 30 --fps 30 --out $f `
      --disk-stop-mb $stopMb --disk-warn-mb ($stopMb+50) }
```

```
run1 stop=1 exit=0 size=2576351 part=False decode_errors=True
run2 stop=1 exit=0 size=5057790 part=False decode_errors=True
run3 stop=1 exit=0 size=5199941 part=False decode_errors=True
```

（`decode_errors=True` = 该字段的判据名为"解码零错误"，即 `ffmpeg -v error` 无输出。）

| 检查点 | 结果 |
| --- | --- |
| `disk_stop=1` | 3/3 |
| 成品可播（ffmpeg 全解码零错误） | 3/3 |
| 无 `.part` | 3/3（`part=False`） |
| 无 0 字节文件 | 3/3（2.5–5.2MB，> 100KB 断言亦通过） |
| 三次结果一致 | `disk-stable-3x` PASS（判定元组一致；停止时刻本身允许抖动） |

---

## §2 ER-02 强杀 ×3 时机 + ER-01 代理标注

### 2.1 ER-01 的性质（报告首行）

> **代理验证，非物理断电**：本机没有可控的断电手段，ER-01 按 `docs/05 §7.2` 4 与 `docs/13` 的约定，
> 用 **ER-02 进程强杀**代理；它证明"进程被瞬间终止后容器仍可恢复"，**不证明**掉电场景的磁盘写缓存行为。
> 本条不得升格为真机断电结论。

### 2.2 三时机 ×3（`g5_inject.ps1`）

```powershell
pwsh -File verify/cases/g5_inject.ps1
```

```
--- ER-02 强杀 ×3（录制中 / 暂停中 / 收尾前）---
  INFO loss=3.03s (kill=8s, recovered=4.97s)
  PASS kill-recording-run1 — repairable=True playable=True audio=True dur=4.97s
  PASS kill-recording-run2 — repairable=True playable=True audio=True dur=4.97s
  PASS kill-recording-run3 — repairable=True playable=True audio=True dur=4.97s
  PASS kill-recording-stable-3x — 3 次结果一致
  INFO loss=4.03s (kill=9s, recovered=4.97s)
  PASS kill-paused-run1..3 — repairable=True playable=True audio=True dur=4.97/4.99/4.97s
  PASS kill-paused-stable-3x — 3 次结果一致
  INFO loss=2.78s (kill=4.7s, recovered=1.92s)
  PASS kill-finalizing-run1..3 — repairable=True playable=True audio=True dur=1.92/1.90/1.92s
  PASS kill-finalizing-stable-3x — 3 次结果一致
```

### 2.3 补充：90s 长样本（把"≥ 原时长−10s"放在有意义的量级上）

用例里的 kill 点只有 4.7–9s，"丢 ≤10s"这条判据形同虚设，所以另做一次 90s 样本：

```powershell
$p = Start-Process sr-recorder.exe -PassThru -ArgumentList @('--record','--seconds','180','--fps','30','--out','long.mkv')
Start-Sleep -Seconds 90; Stop-Process -Id $p.Id -Force
sr-recorder.exe --scan <dir>; sr-recorder.exe --repair long.mkv.part
```

```
scanned=1
file=long.mkv.part    repairable=1
reason=video_blocks=58 last_pts=87445ms valid_end=70070010 lost_bytes=983410
probe_repairable=1    repair=ok
output=long.mkv.recovered.mkv
```

`ffprobe`：`codec_type=video` + `codec_type=audio`，`duration=**87.445**s`；`ffmpeg -v error` 全解码**无输出**。

| 判据 | 结果 |
| --- | --- |
| `repairable=1` | ✔ |
| 恢复时长 ≥ 原时长 − 10s（90s − 10s = 80s） | ✔ 87.445s（损失 2.5s） |
| 含音轨 | ✔ |
| 可播（全解码零错误） | ✔ |

---

## §3 回归（提交前门槛）

```powershell
pwsh -File verify/cases/g5_mp4.ps1
pwsh -File verify/cases/g5_multiaudio.ps1
pwsh -File verify/cases/g5_inject.ps1
pwsh -File verify/cases/g4_gate.ps1
pwsh -File verify/cases/ui_smoke.ps1     # ×3
```

```
RESULT: 19 PASS / 0 FAIL        g5_mp4        （含 r48-mp4-box-sample-table 43/0）
RESULT: 10 PASS / 0 FAIL        g5_multiaudio
RESULT: 28 PASS / 0 FAIL        g5_inject
RESULT: 12 PASS / 0 FAIL        g4_gate
RESULT: 103 PASS / 0 FAIL / 0 SKIP   ×3      ui_smoke
```

```powershell
python verify/tools/contract_check.py            # 汇总: 8 项检查, 8 PASS, 0 FAIL
python verify/tools/coverage_report.py --check   # coverage table is up to date（111 / 1 / 4）
python verify/tools/card_consistency_check.py    # 65 张卡，0 处冲突
python verify/tools/domain_table_check.py        # MISMATCH_COUNT=0
```

> 说明：以上全部在 **LT-01 启动之前**跑完。LT-01 运行期间按 `docs/20:158` 未跑任何用例或构建
> （锁文件 `.lt01-running.json` 存在期间禁止并发跑用例）。

---

## §4 长稳

### 4.1 LT-02 第 2 次（30min 静音 + 恢复段无爆音）— **8/0 PASS**

```powershell
pwsh -File verify/cases/g5_lt02.ps1        # 默认 MuteSeconds=1800 RecoverySeconds=30
```

```
=== LT-02 静音段：1800s（期间不播放任何声音）===
  PASS mute-exit — exit=0
  PASS mute-mean-50db — mean=-91dB max=-91dB
=== LT-02 恢复段：30s，第 5s 起播放 1kHz/10s ===
  PASS recovery-exit — exit=0
  PASS recovery-audio-present — mean=-38.7dB max=-28.7dB
  PASS recovery-no-clip — max=-28.7dB
  PASS recovery-onset-found — onset=5.044921s
  PASS recovery-no-pop — 在 4.944921~5.444921s max=-28.9dB
  PASS recovery-duration — duration=29.999
RESULT: 8 PASS / 0 FAIL
```

现场冻结（`verify/artifacts/lt02-20261118-140033/`，全部在跑完后复制并记 SHA256）：

| 文件 | 大小 | SHA256 |
| --- | ---: | --- |
| `lt02-mute.mkv` | 1,444,959,944 B | `9DECDC86FBEB79001302328700695A9D4F9544EB0AC77DBD8550AB1DE8A9478A` |
| `lt02-recovery.mkv` | 23,345,661 B | `8B37FD9A0EDA2994AC68C213DE817FA6F09F218269210E2E676DF5840B2E1E3C` |
| `mute.log` | 167,422 B | `AC5F02FD42ED87725F6855C8CF7AB7E518694D1251D2FEDF6D32A9779757BF70` |
| `recovery.log` | 2,904 B | `415C38C31C1D411EF817B476981CCE9271201D90A5D2F7DC4C4CE0D5C2605C2B` |
| `summary.json` | 242 B | `CDAF083E9494182AA91D529AE1F533CC2068DA4D906D9B4619E947DBBF256807` |
| `tone.wav` | 960,078 B | `BDCED9A032B4FA761E4437314658C261990D23B3D9B48C5909C06055C2AA46B9` |

### 4.2 LT-01 第 2 次（8h 单文件）— **交付时运行中，未完成**

```powershell
pwsh -File verify/cases/g5_lt01_real.ps1      # 后台启动，O0 已批准占用 8h
```

启动与前置（按卡要求逐项核对）：

```
前置：D:\ 可用 79.7GB（判据 ≥35GB）
启动：pid=73840 seconds=28800 out=verify\artifacts\lt01-real-20261119-094945\lt01-8h.mkv image=sr-recorder-lt01
锁：verify/artifacts/.lt01-running.json（跑前确认不存在 → 已写入）
电源：AC 下「睡眠=从不、熄屏=从不」（powercfg 查询为零），未改系统设置
```

早期健康检查（启动后 ~2 分钟）：

```
pid 73840  WS=554.2MB  CPU=42s
```

- 预计出结果：**2026-11-19 17:49**（启动 + 8h）。`summary.json` 与 16 个漂移采样点在
  `verify/artifacts/lt01-real-20261119-094945/`。
- **本报告不据此判通过**；`docs/07` 轮 51 行也写明"结果待收"。
- 收结果的方法（验收方/本人）：

```powershell
Get-Content verify/artifacts/lt01-real-20261119-094945/summary.json -Raw
Get-Content verify/artifacts/lt01-real-20261119-094945/samples.jsonl -Tail 5
Get-ChildItem verify/artifacts/lt01-real-20261119-094945 | Select-Object Name,Length
```

---

## §5 `R1-49` 收尾实测：≥1GB MP4 的 faststart 代价（顺带发现 `F1-51`）

```powershell
$exe = ".\build-msvc\bin\Release\sr-recorder.exe"
Measure-Command { & $exe --record --seconds 240 --fps 30 --bitrate 40000 --format mkv --out base.mkv }
Measure-Command { & $exe --record --seconds 240 --fps 30 --bitrate 40000 --format mp4 --out base.mp4 }
python verify/tools/mp4_box_check.py base.mp4
ffmpeg -hide_banner -v error -i base.mp4 -f null -
```

| 项 | MKV（对照） | MP4（faststart） |
| --- | --- | --- |
| 时长 | 240s | 240s |
| 文件大小 | 1,150.1 MB | 1,150.1 MB |
| SHA256 | `BC42DD7F…A849A63` | `46597BF7…9DAF87E` |
| 墙钟（含 preflight/countdown/录制/收尾） | **240.44s** | **241.04s** |
| 解码 | — | `ffmpeg -v error` **无输出**（零错误），`duration=240.024671` |

**收尾代价（差分口径）**：`241.04 − 240.44 = **0.60s**`（同一构建、同长度、同码率；MKV 收尾只是回填
Segment 长度，量级 ~0.1s，可忽略）。折算 **≈0.52 s/GB**（读 1.15GB + 写 1.15GB ≈ 2.3GB I/O）。

> 口径声明（不许外推过头）：本机是 NVMe + 1.15GB 文件基本落在页缓存里，写也没有 `fsync`，
> 所以 0.6s 是**乐观值**。按顺序读写 42GB（8h/21GB 的读+写）保守估算：
> NVMe(2GB/s) ≈ 20s；SATA SSD(500MB/s) ≈ 85s；HDD(150MB/s) ≈ 280s。
> 因此 `R1-49` 的结论是：**SSD 上可接受，HDD 上超过 60s 阈值**，需要在 HDD 场景改用"预留 moov 空间（free box）+ 回填"。

**`F1-51`（新发现）**：同一份 240s 素材的 `moov` 结束偏移 = **198,052 B**：

```
PASS mdat-payload-consistent — mdat 数据区 [198068,1205958660) / 文件长度=1205958660
PASS moov-before-mdat — moov_off=32 < mdat_off=198052（faststart 布局）
FAIL moov-in-first-64k — moov 结束偏移=198052 <= 65536
RESULT: 42 PASS / 1 FAIL
```

对比 5s 样本（`g5_mp4`）：`moov` 结束 5,232 B → 43/0 全绿。机制与两种裁定方向见 `docs/26 §7`。
**本轮按 O0 指示：不改判据、不改 `Mp4Writer`，只记录。**

---

## §6 rc 反馈通道：issue 模板

```powershell
python -c "import yaml; d=yaml.safe_load(open('.github/ISSUE_TEMPLATE/bug_report.yml',encoding='utf-8')); ..."
```

```
yaml_ok
required= ['build-sha', 'diag-package', 'repro', 'container', 'expected-actual']
types= ['markdown', 'input', 'textarea', 'textarea', 'dropdown', 'textarea', 'input', 'checkboxes']
name= Bug report（rc / V1.0）
top_missing= []
STRUCT OK items= 8 ids= ['build-sha','diag-package','repro','container','expected-actual','environment','checks']
dup_ids= False
```

- 必填四项（卡要求）+ 一项：`build-sha`（`VERSION.txt` 的 `source_fingerprint`）、`diag-package`、
  `repro`、`container`（是否 MP4）、`expected-actual`。
- **未在 GitHub 上真实提交样例 issue**：本机为 `JeffKana/Screen-Recording`，提交 issue 需要 token，
  而本轮网络仍走 7890 代理、无可用凭据。替代证据是上面的"YAML 可解析 + 结构自检（GitHub issue-form
  的必填字段规则）"。这一条按"未做"记入 `HANDOFF §3`，验收方可在有权限时用模板开一条样例 issue 补证。

## §7 文档与台账

| 文件 | 改动 | 证据 |
| --- | --- | --- |
| `docs/23` | 新增 §8「rc 期缺陷分级与发 rc.2 的条件」（§8.1 分级表 / §8.2 四个触发 / §8.3 必附四段 / §8.4 谁定级） | 文件末节 |
| `docs/24` | 表下加「rc.2 预留（未发布，不写行）」+ 当前不满足触发条件的依据 | 文件 :9 |
| `docs/25` | 新建：四条真机需求表（缺什么 / 谁能提供 / 首轮做什么 / 判定方式 / 前置） | 全文 |
| `docs/26` | 新建：轮 26–50 缺陷分级重列（84 项）+ §7 新发现 `F1-51` | 全文 |
| `docs/07` | 主表轮 51 行（结论/发现留白）+「轮 51 记录」节 | :113 / 「## 轮 51 记录」 |
| `docs/INDEX.md` | 登记 25 / 26 | 编号表 |
| `README.md` | 顶部加「安装与升级（rc 期）」：先 `uninstall.ps1` 再 `install.ps1`，并写原因 | 文件 :8-20 |

---

## §8 PR 与 CI

```powershell
gh pr create --base main --head round/51 --title "轮 51 · 发布复核（ER）+ GA 前置（LT-02 / 缺陷分级 / rc 反馈通道）" --body-file tasks/G6/V1-023/artifacts/pr-body.md
gh pr checks 15
```

```
https://github.com/JeffKana/Screen-Recording/pull/15

build (mingw, warnings=0)            pass  1m4s
build (msvc, warnings=0)             pass  53s
checks (contract/coverage/cards/domain)  pass  13s
```

CI 三绿 ✔（远端跑，与本机 LT-01 长跑互不干扰：CI 在 GitHub runner 上构建，
本机只做只读检查，未在长跑窗口内启动本地构建）。
