# EVIDENCE: G5/S6-001（诊断包真实截断）

> 口径声明: 用**真实 60 分钟会话日志**（`verify/artifacts/cbr60/cbr60.log`，3582 行）验证，
> 不是合成日志；验证对象是导出的 `log.txt` 实际跨度，不是函数单测。
> 数据来源: 2026-10-26 13:19 / `build-msvc` Release

## M8-14#1 一键导出含「最近日志」的诊断包

- 验证方式: 拿 60min 真实 recorder 日志跑 `--export-diag`，比对源日志与产物 `log.txt`
- 命令: `pwsh -NoProfile -File verify/cases/g5_diag_real.ps1 -LogFile verify\artifacts\cbr60\cbr60.log -BuildDir build-msvc -Config Release`
- 输出: `verify/artifacts` 外临时目录（脚本每次自建），断言 7/7
- 结论: **通过**

| 断言 | 实测 | 判据 |
| --- | --- | --- |
| `source-is-long-session` | 源 3,582 行，跨度 **3,599s** | ≥28min（真实长会话） |
| `diag-pack-exported` | `diag_pack=ok`，4 个文件 | 四件套齐 |
| `diag-has-recorder-section` | `log.txt` 含 `=== recorder log (tail) ===` | 有 recorder 段 |
| `diag-span-within-10min` | 导出跨度 **600s** | ≤660s |
| `diag-keeps-newest` | 末条 `t=273177890` = 源末条 `t=273177890` | 保留最新 |
| `diag-under-400kb` | `log.txt` **56,153 字节** | ≤400KB |
| `diag-dropped-old-lines` | 3,582 行 → **596 行** | 老行被裁掉 |

## 修复前后对照

| 项 | 修复前（轮 31 之后的代码） | 修复后 |
| --- | --- | --- |
| `sr-recorder --export-diag --log-file X` 的 log.txt | 141 字节，只有 `(recorder log unavailable)` | 56KB，含最近 10 分钟 |
| 源日志 X | **被 Init 以 "wb" 打开，截成 0 字节** | 原样保留（Init 不再接管输入日志） |
| 合成日志自检 `--diag-window-test` | 通过（但只证明函数对） | 保留，作为函数级回归 |

> 踩坑记录（D-54）：`--export-diag` 把 `--log-file` 当输入读，却又在 `diag::Init` 里以 `wb`
> 打开同一个路径，等于先清空再读。第一次跑这个用例时把轮 31 的
> `lt01-r1-s1.log`（159KB）截成了 0 字节——这是**真实缺陷**，不是用例问题。
> 该文件无法恢复，已改用同批次的 `lt01-r1-s2.log` / `cbr60.log` 复核，并在 HANDOFF 里写明。

## 复现环境

本机 / Windows 11 24H2 / MSVC 14.44 Release
