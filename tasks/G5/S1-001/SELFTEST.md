# SELFTEST: S1-001

## 1. 构建

```powershell
# MSVC（VS 内置 cmake 不在 PATH）
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build-msvc --config Release
# → sr-recorder.vcxproj -> build-msvc\bin\Release\sr-recorder.exe，零告警

# MinGW（ninja + winlibs 工具链）
$mingw='C:/Users/98468/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin'
$env:PATH="$mingw;$env:PATH"
& '...\cmake.exe' --build build-mingw
# → [2/2] Linking CXX executable bin\sr-recorder.exe，零告警
```

## 2. 用例

```powershell
pwsh -NoProfile -File verify/cases/g3_output.ps1 -BuildDir build-msvc
# RESULT: 15 PASS / 0 FAIL

python verify/tools/contract_check.py
# 汇总: 7 项检查, 7 PASS, 0 FAIL

pwsh -NoProfile -File verify/cases/g3_storage.ps1 -BuildDir build-msvc
# RESULT: 11 PASS / 0 FAIL

pwsh -NoProfile -File verify/cases/g3_recovery.ps1 -BuildDir build-msvc
# RESULT: 9 PASS / 0 FAIL
```

## 3. 断言数变更说明

指令预期 13 条，实际 15 条：D1、D2 各含 2 条断言（行为断言 + 成品可播/文件名断言），覆盖不减只增。

## 4. 过程中踩到的坑（下一轮务必注意）

**问题**：为复现 D-24 临时回退源码后，用 `Copy-Item` 从备份恢复修复版再构建，MSBuild 判定 `.obj` 比源码新而**跳过重编**，随后用例以旧二进制运行，D1 报 FAIL（14 PASS / 1 FAIL），一度误判为修复无效。

**证据**：源码 `LastWriteTime=11:28:02`，`OutputPath.obj` `11:29:02`（obj 更新）；构建日志无 `OutputPath.cpp` 编译行。

**处置**：`(Get-Item src/recorder/OutputPath.cpp).LastWriteTime = Get-Date` 后重编，日志出现 `OutputPath.cpp`，用例回到 15/15。

**结论**：凡「恢复源码 / 打补丁后重建」，必须确认构建日志里出现该文件的重编行，否则验收结论无效。这条比 D-24 本身更容易骗人。

## 5. 复现 D-24 的最小步骤（留档）

```powershell
# 1) 把 CommitOutput 的收尾避让临时改为直接 rename
# 2) 重编（确认日志有 OutputPath.cpp）
# 3) 后台录 3s 到 race.mkv，1.5s 时写入 32 字节哨兵
$p = Start-Process -FilePath .\build-msvc\bin\Release\sr-recorder.exe -PassThru `
     -RedirectStandardOutput race.log -ArgumentList @('--record','--seconds','3','--fps','30','--out',"`"$race`"")
Start-Sleep -Milliseconds 1500
[System.IO.File]::WriteAllBytes($race, [byte[]](1..32))
$p.WaitForExit()
# 旧行为：race.mkv 1424401 字节，哨兵被覆盖，output=race.mkv
# 新行为：race_001.mkv 为成品，race.mkv 仍 32 字节
```
