# SELFTEST: M6-03 / M8-14（可复现自检）

> 每条都能在本机直接重跑；括号里是判定依据。

## 1. 指标链路（M6-03）

```powershell
pwsh -NoProfile -File verify/cases/g5_diag.ps1 -BuildDir build-msvc -Config Release
```

- A 段断言：recorder 每秒一行 `metrics capture_fps=… encode_fps=… dropped=… captured=…`，
  采样间隔 1000±15ms；`--inject-drop-every 10` 时 `dropped>0` 且汇总 `frames_dropped>0`。
- A2 段断言：UI 状态栏含「采集 x / 编码 y fps · 丢帧 n」，且 UI 日志有同值 `snapshot … capture_fps=`。

## 2. 诊断包（M8-14）

```powershell
build-msvc\bin\Release\sr-ui.exe --export-diag $env:TEMP\sr-diag
```

- 目录内恰好 4 个文本文件；整包 <5MB；`diag_pack ok=1 files=4 bytes=… ms=…`。
- `g5_diag` B 段额外断言：无 PNG/JFIF/ftyp/ID3/RIFF 签名、用户名命中 0、`<2s`。

## 3. 时间窗裁剪（本轮新增，确定性自检）

```powershell
# 生成 2 小时合成日志（每 10s 一行）后：
build-msvc\bin\Release\sr-recorder.exe --diag-window-test $env:TEMP\synth.log
# 期望：span_ms=600000，dropped_lines>0
```

`TruncateByAge` 是纯函数，`--diag-window-test` 只在 `SR_TEST_HOOKS=ON` 构建里存在。

## 4. 日志分级

```powershell
build-msvc\bin\Release\sr-recorder.exe --record --seconds 3 --out $env:TEMP\a.mkv --log-level debug --log-file $env:TEMP\a.log
# debug > info > warn 行数；debug 有帧级行，warn 无 info 指标行
```

## 5. 覆盖表三态 / C8

```powershell
python verify/tools/coverage_report.py --self-test   # self_test ok
python verify/tools/coverage_report.py --check       # up to date
python verify/tools/contract_check.py                # 8/8
```

自检会构造「有卡未实现」的 stub 卡，证明该状态不会被算成已实现。
