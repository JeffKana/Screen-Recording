# HANDOFF · V1-025（轮 53 · 0.1 MVP 发布收尾）

## 1. 我做了什么

| 类 | 内容 |
| --- | --- |
| 文档口径 | `docs/22`（⑤⑥ + 4 条真机 →「1.0 前置 / 0.1 例外」；① 行列 5 条例外 EX-02~06；发布时点头注）、`docs/09`（4 条 + `M1-27#1` 改「0.1 范围外（docs/27 §3）」，**状态仍 111/1/4**）、`docs/23 §4`（改「转 1.0-GA 的五条硬条件」+ 两套门槛区分）、`docs/24`（rc.1 备注「已被 0.1 取代」+ 0.1.0 行）、`README`（横幅 0.1 + 兼容性范围指向）、`RELEASE-NOTES`（已知限制第 4 条按 faststart 实况更新） |
| 出包 | `build_release.ps1` 的 `channel` 改三态（`-rc.`→rc / `0.1.0`→mvp / 其余→ga），`mvp` 强制 `ga_ready=0`；`-Version 0.1.0` **17/17**，zip 375,574B、SHA256 `23B9E7D9…`、`source_fingerprint=FFD3DF70…` |
| 冒烟 | 干净目录：解压 → `install.ps1` → 录 60s（59.999s、0 error）→ `uninstall.ps1` → 程序目录/卸载登记清空、**用户成片保留** |
| 发布 | `v0.1.0`（**非 pre-release**）标题「0.1（MVP，仅本机验证）」，资产 zip 已挂；`docs/24` 补行、`docs/22` 回写发布时点 |

## 2. 我是怎么验证的

- 每条判据一行证据：`EVIDENCE.md §1`（文档改动前后对照 + `coverage_report --check` + `docs/27` 引用面）、`§2`（17/17 + VERSION.txt 三态 + SHA）、`§3`（冒烟三步原始输出，含一次写失败的取证与重做）、`§4`（release 属性与资产 digest）。
- 静态检查：`SELFTEST.md §2`（契约 8/8、覆盖 PASS、卡面 0 冲突、文件域 0 mismatch）。
- 口径自查：`docs/09` 里 `BLOCKED|缺机器待排期` **0 命中**、`0.1 范围外` **5 命中**（4 子句 + `M1-27#1`）。

## 3. 哪些需求我**没**做完（逐条，不得计为通过）

| 项 | 状态 | 说明 |
| --- | --- | --- |
| **例外 EX-02~EX-06** | **未测** | 0.1 范围冻结（`docs/27 §3`）；发布说明只写「未测」，不写「兼容/已验证」。1.0 前置 |
| **LT-01 第 3 次结论** | **未出结果** | 轮 52 于 13:37 启动，预计 21:37 出 `verify/artifacts/lt01-real-20261121-133707/summary.json`；0.1 的发布条件只要求「单机 8h ≥2 次」，第 3 次不构成发布前提 |
| **`R1-52`（并发抢同一 `--out`）** | **未修** | 级别待裁（建议一般）；0.1 按「用户可见路径不可达」放行，修复排期由 O0/验收方定 |
| **`round/52` 的验收** | **未做** | 执行方不自评；PR #19 待验收方独立重跑 |

## 4. 两件必须由 O0 处理的事

### 4.1 合并顺序（本轮是叠分支）

- `round/53` 基于 `round/52`（`docs/20 §6` 要求「从 main 拉」，本轮因「轮 52 合并后再开」而无法满足 → **偏差已登记**）。
- **必须按 `round/52` → `round/53` 顺序合并**；合并第二个分支时 `docs/07` 会在同一区域冲突（两行台账），
  按「**两行都留**」解冲突即可，无代码影响。
- 合并后 `v0.1.0` 的 tag（指向 `53e6c61`）会成为 `main` 的祖先；如希望 tag 直接落在 `main` 上，可在合并后
  重新打一个 `v0.1.0` 的 lightweight tag（**不要覆盖已发布的 release 资产**）。

### 4.2 `R1-52` 的级别

- 现象：两个 `--record` 进程在同一毫秒窗口抢同一 `--out` → `.part` 互写、产物 EBML 损坏，两进程 `exit=0`。
- 若判**阻塞**：`docs/27 §4` 第 1 条「阻塞 0」不成立 → 0.1 应撤回/暂停，先修 `OutputPath` 的原子占位；
- 若判**一般**（我的建议）：0.1 维持已发布状态，修复进 `0.1.1`。

## 5. 验收方请重跑这些

```powershell
# 1) 文档口径四处一致 + 覆盖表未变
python verify/tools/coverage_report.py --check
python verify/tools/contract_check.py
Select-String -Path docs/09-V1.0覆盖表.md -Pattern "BLOCKED|缺机器待排期"   # 期望 0 命中

# 2) 包与 release
pwsh -File verify/tools/build_release.ps1 -Version 0.1.0   # 期望 17 PASS / 0 FAIL
Get-FileHash dist\ScreenRecorder-0.1.0-win-x64.zip -Algorithm SHA256   # 23B9E7D9…
gh release view v0.1.0 --json tagName,name,isPrerelease,assets           # isPrerelease=false

# 3) 冒烟三步（自己解压一遍，别信我的日志）
#    解压 → install.ps1 → 录 60s → uninstall.ps1 → 看程序目录/卸载登记/用户成片
```
