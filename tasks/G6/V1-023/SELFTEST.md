# SELFTEST · V1-023（轮 51）

## §0 开工四项自查（`docs/06 §4.2`）

| 检查项 | 通过标准 | 本轮实测 | 结论 |
| --- | --- | --- | --- |
| 需求编号可检索 | 卡里提到的需求号能在 `docs/01` 查到 | M6-01/M6-04/M6-05/M6-08 在 `docs/01` 有原文行；M1-27 在 `docs/05 §7.2.1` EX-01；M3-09 在 `docs/01:111` | ✅ |
| 判据来源可定位 | 判据出自文档而非口头 | `docs/05 §7.1`（分级）/`§7.2`（六条）/`§7.2.1`（例外）、`docs/23 §3/§4`（rc 门禁 vs GA 五条）、`轮51指令.md` 通过判据 1–6 | ✅ |
| 依赖是否就绪 | 上游构建、磁盘、窗口 | `main` @ `4b117cf`；`build-msvc` Release（SHA 见 `EVIDENCE.md §0`）；D: 84.5GB；`verify/artifacts/.lt01-running.json` **不存在**；AC 下不睡眠/不熄屏 | ✅ |
| 上一轮遗留 | 验收报告的 Required 项要有去处 | `R1-49` 本轮实测（`EVIDENCE.md §5`）；`R2-49`/`R3-49` 与 `F2-48` 合并登记 V1.1（`docs/26 §2.1`）；`F1-51` 本轮新发现进 `docs/26 §7` | ✅（LT-01 未完成，见 §3） |

---

## §1 命令与输出（可逐条重跑）

```powershell
# 0) 指纹
git rev-parse --short HEAD                       # 4b117cf
Get-FileHash build-msvc/bin/Release/sr-recorder.exe -Algorithm SHA256
#   BAF0A783917623B3BDD717CC07238C420183A5F50E1929621DA49FD4F27AA26C

# 1) ER（P0-1）
pwsh -File verify/cases/g5_inject.ps1            # RESULT: 28 PASS / 0 FAIL  （1.6min）

# 2) LT-02 第 2 次（P0-2f）
pwsh -File verify/cases/g5_lt02.ps1              # RESULT: 8 PASS / 0 FAIL

# 3) LT-01 第 2 次（P0-2e，8h，独占）
pwsh -File verify/cases/g5_lt01_real.ps1         # 2026-11-19 09:49:45 启动，交付时运行中

# 4) 回归（提交前门槛）
pwsh -File verify/cases/g5_mp4.ps1               # 19 / 0
pwsh -File verify/cases/g5_multiaudio.ps1        # 10 / 0
pwsh -File verify/cases/g4_gate.ps1              # 12 / 0
pwsh -File verify/cases/ui_smoke.ps1  ×3         # 103 / 0 / 0 SKIP ×3

# 5) 静态
python verify/tools/contract_check.py            # 8 PASS / 0 FAIL
python verify/tools/coverage_report.py --check   # up to date（111 / 1 / 4）
python verify/tools/card_consistency_check.py    # 65 卡 0 冲突
python verify/tools/domain_table_check.py        # MISMATCH_COUNT=0

# 6) 收尾实测（R1-49）
& sr-recorder.exe --record --seconds 240 --fps 30 --bitrate 40000 --format mp4 --out base.mp4
python verify/tools/mp4_box_check.py base.mp4    # 42 PASS / 1 FAIL（moov-in-first-64k，见 F1-51）
```

---

## §2 构建

| 构建 | 本轮 | 说明 |
| --- | --- | --- |
| MSVC Release | ✅ 重建零告警（2026-11-18 13:56） | ER/LT/收尾实测都用它 |
| MSVC Debug | ⛔ 未重建 | `src/` 本轮零改动（`git diff ed8767a..HEAD -- src/` 空）；轮 49 已验零告警；8h 窗口内不跑构建（会污染丢帧/内存基线） |
| MinGW Release | ⛔ 未重建 | 同上；CI 三平台会重建 |

> 这一项按"未做"记入 `HANDOFF §3`，不拿轮 49 的结论冒充本轮。

---

## §3 LT-01 的状态（不计入通过）

- 启动：`2026-11-19 09:49:45`，pid 73840，`seconds=28800`，镜像 `sr-recorder-lt01.exe`；
- 锁：`verify/artifacts/.lt01-running.json` 已写入（跑前确认不存在）；
- 早期健康：+2min 时 WS=554.2MB、CPU=42s；
- **交付时未完成**：预计 17:49 出 `summary.json`；本轮不得据此判通过，`docs/07` 轮 51 行同样标注。

---

## §4 交付包自检

```powershell
Get-ChildItem tasks/G6/V1-023 -File | Select-Object Name
```

```
TASK.md  EVIDENCE.md  SELFTEST.md  HANDOFF.md
（artifacts/ 不入库：er04/run1..3、er02-long、mp4-close、r51-*.out、github-issue-forms.schema.json）
```

## §5 交付前逐项确认

- [x] 每条需求都有对应证据（`EVIDENCE.md §1–§7`）
- [x] 本轮无新增断言，故无"先红后绿"；**未改 `verify/` 下任何断言**
- [x] 相关回归零失败（`§3`）；跑回归的时间点已说明（LT-01 启动前）
- [x] 未超出文件域（`git status --short` 只有 `docs/`、`.github/ISSUE_TEMPLATE/`、`README.md`、`tasks/`）
- [x] 未改 `src/`、未改判据、未改 `docs/01`/`docs/05`
- [x] 文档已同步：`docs/07`（留白 + 记录）、`docs/23 §8`、`docs/24`、`docs/25`、`docs/26`、`docs/INDEX`、`README`
- [x] 已知限制与未做项写明（`HANDOFF.md §3`：LT-01 未完成、issue 未真实提交、Debug/MinGW 未重建、四条真机未测）
- [x] 不自评「已验收」
