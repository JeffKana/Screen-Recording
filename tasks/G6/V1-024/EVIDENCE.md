# EVIDENCE · V1-024（轮 52）

> 每条证据都带**可重跑命令**与**原始输出**。验收方独立重跑时，数值允许有小幅波动（时间类 ±）、
> 结论不允许不同。本轮所有录制都在同一个 exe 上完成：

| 项 | 值 |
| --- | --- |
| exe | `build-msvc/bin/Release/sr-recorder.exe` |
| SHA256 | `2026BCCC6BE7E10E3B45C9D055D8DBD75D680FF1D57C7A9469BF965D45B21575` |
| 构建 | MSVC 14.44 Release（`SR_TEST_HOOKS=ON`，用例需要 `--mp4-reserve`）/ MinGW GCC 16.2 / MSVC Debug 三构建零告警 |
| 机器 | Win11 build 26200 / i5-13500 / Intel UHD 770 / 单屏 100% / 系统盘与写入盘均为 NVMe（`C:` 与 `D:` 在同一物理盘） |
| 上游 | `main` @ `91b96c1` |

---

## §1 P0-1 faststart 改「预留 moov + 回填」——五条判据

### 1.1 实现要点（改动只在 `src/mux/Mp4Writer.*` + 三处接线）

| 环节 | 改动 |
| --- | --- |
| 开录（`Open`） | `ftyp` 之后写一个 `free` box 占位，payload 不落盘（空洞），`mdat` 从预留区之后开始。预留大小 = `4096 + 16 × (预计视频样本数 + 预计音频样本数)`，8 字节对齐，上限 64MB；输入是 `--seconds` / `--fps`（`Mp4Writer::SetFaststartReserveHint`） |
| 收尾（`Close`） | moov 生成后**直接写进预留区**：`offset_shift = 0`，`mdat` 一个字节都不搬；预留区剩余部分补一个顶层 `free` box（payload 是开录留下的零，不重写） |
| 预留不足 | `moov > 预留` 时回退旧路径：`mdat` 后移 `moov - 预留`，并置 `FaststartFallback()`；`RecordingSession` 收尾打一行 `faststart_mode=move … faststart_fallback=move` |
| 8 字节对齐 | moov 若不足 8 的倍数，在 moov **内部**追加一个 9~15 字节的 `free` 子 box —— 保证「预留区 − moov」只可能是 0 或 ≥8，剩余空间总能落一个合法 box（不会剩 1~7 字节的非法空隙） |
| 观测 | 收尾日志：`faststart_mode=reserve\|move faststart_reserve=<B> faststart_moov_bytes=<B> faststart_close_us=<µs>` |
| 调试开关 | `--mp4-reserve <bytes>`（`SR_TEST_HOOKS=ON` 才有）：人为指定预留区，用于走回退路径 |

### 1.2 判据 ① 1.15GB/240s stop→可播 增量 ≤1s（对比同参数 MKV）

```powershell
pwsh -File verify/cases/g5_faststart_perf.ps1 -Seconds 240 -BitrateKbps 40000
# 产物：verify/artifacts/faststart-20261121-105710/（summary.json + 两次 .cmd.txt + box-check.txt）
```

| 指标 | 实测 | 判据 |
| --- | --- | --- |
| 素材 | 240s / 30fps / 40,000kbps CBR → MP4 **1,206,069,120 B（1.123GB）**、MKV 1,205,992,837 B | — |
| MP4 faststart 收尾（`faststart_close_us`，只含 moov 生成 + 回填 + 落盘） | **976 µs（0.976 ms）** | ≤1,000,000 µs |
| 交叉校验：子进程总墙钟 | MP4 **240.615s** vs MKV **240.573s** → 差 **+42 ms** | ≤1s（含进程启动噪声） |
| 旧实现（轮 51 同参数实测，`验收报告-轮51 §R2`） | 0.60s（≈0.52 s/GB，HDD 21GB 外推 ≈280s） | — |

> 输出行（原始）：
> `PASS fs-perf-mp4-close-under-1s — MP4 faststart 收尾=976us（0.976ms，判据 ≤1,000,000us）；对比同参数 MKV 墙钟差=42ms（240s 素材 / 40000kbps）`
> `PASS fs-perf-mp4-vs-mkv-wall-delta — MP4 墙钟=240615ms vs MKV 墙钟=240573ms，差=42ms（容噪 ±2000ms）`

### 1.3 判据 ② 8h 外推 ≤10s（附计算式）

```
每 GB 收尾 = 0.976ms ÷ 1.123GB = 0.869 ms/GB
8h @ 6Mbps ≈ 6000kbps × 28800s ÷ 8 ≈ 21.6GB ≈ 21GB
外推 = 0.869 ms/GB × 21GB = 18.2ms ≈ 0.02s  ≤ 10s   ✔（余量 500×）
```

> 输出行：`PASS fs-perf-8h-extrapolation — 每 GB 收尾 0.869ms/GB × 21GB（8h@6Mbps 外推）= 0.02s ≤ 10s；计算式：0.976ms ÷ 1.123GB × 21GB`
>
> 口径说明：外推只用「每 GB 收尾耗时」这一个线性系数，**不含** moov 生成本身（8h 的 moov ≈20MB，
> 生成 + 写 20MB 按本机 NVMe 约 20~40ms，仍远小于 10s）。**不是**把 8h 真跑一遍的数据。

### 1.4 判据 ③ 文件布局 + ffprobe/全解零 error

```powershell
python verify/tools/mp4_box_check.py verify/artifacts/faststart-20261121-105710/perf-mp4.mp4
ffprobe -v error -show_entries format=format_name,duration -of default=nw=1 <mp4>
ffmpeg -hide_banner -v error -i <mp4> -f null -
```

| 项 | 实测 |
| --- | --- |
| 顶层 box | `ftyp@0+32 moov@32+198032 free@198064+110448 mdat@308512+1205760608` |
| `moov` 偏移 < `mdat` 偏移 | 32 < 308,512 ✔ |
| 预留区 | `faststart_reserve=308480B`、`faststart_moov_bytes=198032B`、余量 110,448B（占文件 0.009%，播放器跳过） |
| `ffprobe` | `format_name=mov,mp4,m4a,3gp,3g2,mj2`、`duration=240.024671`（录制 240s）、**error 行 0** |
| 全片解码 | `ffmpeg -v error -i … -f null -` → exit 0、**stderr 0 行** |
| `mp4_box_check.py` | **42 PASS / 0 FAIL**（`moov-in-first-64k` 按轮 52 新口径对 >60s 素材 SKIP，见 §2） |

### 1.5 判据 ④ 人为把预留调小 → 走回退路径且产物可播

```powershell
pwsh -File verify/cases/g5_mp4.ps1
# 其中 r52-faststart-fallback-* 两条即本判据；也可手工复跑：
build-msvc\bin\Release\sr-recorder.exe --record --seconds 5 --fps 30 --bitrate 2000 `
    --format mp4 --mp4-reserve 4096 --out fb.mp4
```

| 项 | 实测 |
| --- | --- |
| 日志 | `faststart_mode=move faststart_reserve=4096 faststart_moov_bytes=5200 faststart_close_us=815 faststart_fallback=move` |
| 布局 | `ftyp@0+32 moov@32+5200 mdat@5232+1391441`（回退后无 free box，moov 紧贴 mdat） |
| 可播性 | `duration=5.03873s`（录制 5s ±0.2s）、`ffmpeg -v error` **0 行**、`mp4_box_check` **43 PASS / 0 FAIL** |

### 1.6 判据 ⑤ 回归

| 用例 | 结果 | 原始输出 |
| --- | --- | --- |
| `g5_mp4` | **23 PASS / 0 FAIL**（原 19 条 + 新增 4 条） | `verify/artifacts/r52-g5_mp4-green.txt` |
| `g3_playback` | **11 PASS / 0 FAIL** | `verify/artifacts/r52-g3_playback.txt` |

> `g5_mp4` 由 19 → 23 条的原因：按 §2 把 1 条拆成 2 条（+1），并新增「预留命中」「回退日志」「回退产物可播」
> 「长素材只判通用门槛」4 条，其中「预留命中」是原 `moov-in-first-64k` 的位置被替换后补的观测断言。
> 逐条名字见 `r52-g5_mp4-green.txt`。

---

## §2 P1 `g5_mp4` 判据拆分（R3）+ 先红后绿

### 2.1 拆分口径（依据 `验收报告-轮51 §R3`）

| # | 判据 | 适用范围 | 断言 |
| --- | --- | --- | --- |
| ① | `r52-faststart-moov-before-mdat` | **通用**（与时长/大小无关） | `moov` 偏移 < `mdat` 偏移 |
| ② | `r52-faststart-short-moov-in-first-64k` | **≤60s 短素材** | `moov` 结束 ≤ 65536 |

`verify/tools/mp4_box_check.py` 同步按 `mvhd.duration` 分流：>60s 打印
`SKIP moov-in-first-64k — duration=…ms > 60000ms：长素材只判 moov-before-mdat（F1-51）`，
**不判 PASS 也不判 FAIL**（避免用一条必然失败的断言污染结论）。

### 2.2 长素材（120s）实证：`moov` 结束已在 64KB 之外

```powershell
pwsh -File verify/cases/g5_mp4.ps1   # 内含 120s 样本
```

| 项 | 实测 |
| --- | --- |
| 顶层 box | `ftyp@0+32 moov@32+99568 free@99600+61072 mdat@160672+18740241` |
| `moov` 结束 | **99,600 > 65,536** → 旧「前 64KB」通用门槛在 120s 素材上必然 FAIL |
| 通用门槛 | 32 < 160,672 ✔ |
| `mp4_box_check` | **42 PASS / 0 FAIL**（64KB 项按口径 SKIP） |
| 全解 | 0 error 行 |

### 2.3 红-绿对照表

| 断言 | 红（旧实现：`build-release` 2026-11-18 包内 exe） | 绿（本轮 exe） |
| --- | --- | --- |
| `r52-faststart-reserve-hit` | **FAIL**：`faststart_mode=`（无该日志）/ `reserve=0` / 顶层无 `free` box | **PASS**：`faststart_mode=reserve reserve=18960 moov=5200 收尾=96us`，顶层 `free@5232+13760` |
| `r52-faststart-fallback-log` | **FAIL**：旧 exe 不认识 `--mp4-reserve`，exit=1，无 `faststart_fallback=move` | **PASS**：`exit=0 faststart_mode=move reserve=4096 moov=5200`，日志含 `faststart_fallback=move` |
| `r52-faststart-fallback-playable` | **FAIL**：回退产物不存在（`box=[]`、`duration=-1`） | **PASS**：`ftyp@0+32 moov@32+5200 mdat@5232+1391441`、duration 5.03873s、解码 0 error、box check 43/0 |
| `r52-faststart-long-sample-generic-only` | PASS（旧实现 moov 也前置：`moov@32+99556 mdat@99588`） | PASS：`moov@32+99568 free@99600+61072 mdat@160672` |
| 整轮合计 | `RESULT: 20 PASS / 3 FAIL` | `RESULT: 23 PASS / 0 FAIL` |

```powershell
# 红端（旧构建）
pwsh -File verify/cases/g5_mp4.ps1 -RecorderPath build-release\bin\Release\sr-recorder.exe
# 原始输出：verify/artifacts/r52-g5_mp4-red-old-exe.txt
# 绿端
pwsh -File verify/cases/g5_mp4.ps1
# 原始输出：verify/artifacts/r52-g5_mp4-green.txt
```

> 红端的 3 条 FAIL 都是**本轮新增断言**，旧实现的既有 19 条判据全绿——说明拆分没有放宽既有判据，
> 只是把「前 64KB」从通用门槛降级为短素材启发式，并新增了预留/回退两条可观测断言。

---

## §3 P0-3 `F3-42` 定向复现（30 次）

```powershell
pwsh -File verify/cases/g5_f342_repro.ps1 -Iterations 10
# 产物：verify/artifacts/f342-<时间戳>/{runs.csv, summary.json, *.out}
```

判据「静默退出」的定义（写进脚本，可复核）：退出码非 0 **且** 无 `record_exit=` 行 **且** 输出 <32 字符；**或**
留下 `.part` 但 <4KB 且无成品。

| 场景 | 第 1 次运行（11:11） | 第 2 次运行（11:14，场景②改为服务态） |
| --- | --- | --- |
| ① 两个用例并发（不同 `--out`） | 10 轮 ×2 进程，静默 0 | 10 轮 ×2 进程，静默 0 |
| ② 抢单实例锁 | **未真正生效**：`--record` 路径不取锁（锁只在服务态 `AcquireSingleInstance` 取）→ 已改正 | 10/10 被拒（B 打印 `already running`，exit 4） |
| ③ 抢占同一输出路径 | 10 轮 ×2 进程，静默 0；**产物损坏 2 条**（见 §3.1） | 10 轮 ×2 进程，静默 0、损坏 0 |
| 残留进程 | 0 | 0 |

**结论：`F3-42` 定向复现 30 次（第 2 次运行的 30 个场景执行）+ 第 1 次运行的 30 次场景执行，**
**合计 60 次场景执行、110 次进程级记录，一次未复现**。按 `验收报告-轮51 §R1`，级别保持「未定性观察」，
已写进 `docs/26 §3.1`。

### 3.1 新发现 `R1-52`：并发抢同一 `--out` 时 `.part` 互写 → 产物不可播（非 F3-42）

| 项 | 值 |
| --- | --- |
| 现象 | 两个 `--record` 进程在**同一毫秒窗口**内抢同一个 `--out`：`PrepareOutputPath` 的「`.part` 是否已存在」是**检查后使用**（TOCTOU），两个进程都通过检查、都 `fopen` 同一个 `.part`，写入交错；收尾 `CommitOutput` 再各查一次空位，rename 时可能互相覆盖 |
| 证据 | `verify/artifacts/f342-20261121-111121/`：`s3-2.mkv` 921,867 B，`ffprobe` 报 `EBML header parsing failed`；两个进程 `exit=0`、stdout 完整、无报错 |
| 复现率 | 第 1 次运行 10 轮里 **2/20** 条记录损坏；第 2 次运行 **0/20**（窗口只有几毫秒，需两次启动落在同一 ms 级窗口） |
| 是否产品路径 | UI 走 `--record` 但输出名由 UI 生成且 UI 自身单实例；**用户可见路径无法构造该场景**（需要刻意并发执行两条命令、且 `--out` 完全相同） |
| 本轮处置 | **不改**（属 `src/recorder/OutputPath.cpp` 的原子占位改造，超出本卡文件域）；提单待验收方定级，建议「一般」：产物不可用（按 §7.1 单看现象像阻塞），但仅限并发抢同一路径、非用户可见路径 |
| 关联 | 与 `F3-42`（静默退出、`.part`=108B）**不是同一现象**：本例两个进程都正常收尾并 exit 0 |

---

## §4 P0-2 长稳第 3 次

### 4.1 LT-02 第 3 次（30min 静音段 + 恢复段）

```powershell
pwsh -File verify/cases/g5_lt02.ps1 -MuteSeconds 1800 -RecoverySeconds 30
# 产物：verify/artifacts/lt02-20261121-112759/{summary.json, lt02-mute.mkv, lt02-recovery.mkv, *.log, *.out}
```

| # | 判据 | 实测 |
| --- | --- | --- |
| 1 | 静音段可播且真的是数字静音 | `lt02-mute.mkv` 1,396,721,681 B（1800s），`mean=-91dB` / `max=-91dB`（等于数字静音下限） |
| 2 | 恢复段 `exit=0` | PASS |
| 3 | 恢复段有声音 | `mean=-38.7dB` / `max=-28.7dB` |
| 4 | 不削波 | `max=-28.7dB` ≪ 0dBFS |
| 5 | 起播点可定位 | `onset=4.997506s`（第 5s 起播 1kHz） |
| 6 | 起播无爆音 | 边界窗口 `4.8975~5.3975s` 内 `max=-28.9dB`（与稳态一致，无尖峰） |
| 7 | 时长 | `duration=29.999s`（录制 30s） |
| 8 | 汇总 | **8 PASS / 0 FAIL** |

> 输出行：`RESULT: 8 PASS / 0 FAIL ; 产物 …\lt02-20261121-112759`
> 这是 LT-02 的**第 3 次**独立通过（轮 31 第 1 次、轮 51 第 2 次、本轮第 3 次）。

### 4.2 LT-01 第 3 次（8h 单文件）

<!-- FILL-LT01 -->

---

## §5 P1 issue 模板演练（R4）

```powershell
gh issue create --title "[drill] issue 模板演练（轮 52 R4，创建后即关闭）" --body "…"
```

<!-- FILL-DRILL -->

---

## §6 回归与静态检查（本轮交付门槛）

| 项 | 结果 | 原始输出 |
| --- | --- | --- |
| `g5_mp4` | **23 PASS / 0 FAIL** | `verify/artifacts/r52-g5_mp4-green.txt` |
| `g5_multiaudio` | **10 PASS / 0 FAIL** | `verify/artifacts/r52-g5_multiaudio.txt` |
| `g5_inject` | **28 PASS / 0 FAIL** | `verify/artifacts/r52-g5_inject.txt` |
| `g4_gate` | **12 PASS / 0 FAIL** | `verify/artifacts/r52-g4_gate.txt` |
| `g3_playback` | **11 PASS / 0 FAIL** | `verify/artifacts/r52-g3_playback.txt` |
| `ui_smoke` ×3 | **103/0/0** ×3 | `verify/artifacts/r52-ui_smoke_x3.txt` |
| `contract_check.py` | **8 PASS / 0 FAIL** | 见 `SELFTEST.md §3` |
| `coverage_report.py --check` | `coverage table is up to date`（111/1/4 未变） | 见 `SELFTEST.md §3` |
| `card_consistency_check.py` | 扫描 65 张卡、**0 冲突** | 见 `SELFTEST.md §3` |
| `domain_table_check.py` | **MISMATCH_COUNT=0** | 见 `SELFTEST.md §3` |
| 三构建 | MSVC Release / MSVC Debug / MinGW **零 error 零告警** | 见 `SELFTEST.md §2` |
