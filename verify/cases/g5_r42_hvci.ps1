# 轮 42 · M6-10#2「在启用内存完整性（HVCI）的 Win11 上可正常安装运行」环境实测
#
# 本机是 HVCI 已启用的 Win11（注册表实测），因此这不是模拟/代理：
# 直接在真实 HVCI 环境里跑发布包的 install.ps1 + 包内 exe 录制。
# 验收要点原文（docs/01:205）只要求「可正常安装运行」，不要求额外代码。
#
# Usage: pwsh -File verify/cases/g5_r42_hvci.ps1

param([string]$Zip = 'dist/ScreenRecorder-0.1.0-win-x64.zip')

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$zipPath = Join-Path $root $Zip
$work = Join-Path $env:TEMP 'sr-r42-hvci'
if (Test-Path $work) { Get-ChildItem $work -Recurse -Force | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force -Path $work | Out-Null

$script:pass = 0; $script:fail = 0; $script:skip = 0
function Assert {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    if ($Ok) { $script:pass++; Write-Host ("  PASS {0} — {1}" -f $Name, $Detail) }
    else { $script:fail++; Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail) }
}
function Skip { param([string]$Name, [string]$Reason) $script:skip++; Write-Host ("  SKIP {0} — {1}" -f $Name, $Reason) }
function Field {
    param([string]$Text, [string]$Name)
    if ($Text -match "(?m)^$Name=([^`r`n]*)") { return $Matches[1].Trim() }
    return ''
}
function Resolve-FfmpegBin {
    $onPath = Get-Command ffprobe -ErrorAction SilentlyContinue
    if ($onPath) { return Split-Path -Parent $onPath.Source }
    $packages = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    if (Test-Path $packages) {
        $found = Get-ChildItem $packages -Recurse -Filter 'ffprobe.exe' -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($found) { return $found.DirectoryName }
    }
    return $null
}
$ffbin = Resolve-FfmpegBin

Write-Host '--- M6-10#2 HVCI 环境前置（真实环境，非模拟）---'
$hvciKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity'
$hvci = $null
if (Test-Path $hvciKey) { $hvci = Get-ItemProperty $hvciKey }
$hvciEnabled = ($null -ne $hvci -and $hvci.Enabled -eq 1)
$os = (Get-CimInstance Win32_OperatingSystem).Caption
$isWin11 = $os -match 'Windows 11'
Assert 'm6-10-02-hvci-env' ($hvciEnabled -and $isWin11) `
    ("实测环境：OS={0}，HVCI.Enabled={1}（内存完整性已启用）、WasEnabledBy={2}" -f `
     $os, $(if ($hvci) { $hvci.Enabled } else { 'key missing' }),
     $(if ($hvci) { $hvci.WasEnabledBy } else { 'n/a' }))
if (-not ($hvciEnabled -and $isWin11)) {
    Skip 'm6-10-02-install-run' '本机非 HVCI 启用的 Win11，环境判据不成立（不得用模拟冒充实测）'
    Write-Host ''; Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP" -f $script:pass, $script:fail, $script:skip)
    exit 0
}

if (-not (Test-Path $zipPath)) {
    Skip 'm6-10-02-install-run' "发布包不存在：$Zip"
    Write-Host ''; Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP" -f $script:pass, $script:fail, $script:skip)
    exit 0
}

# 1) 解压发布包（与用户拿到的安装方式一致）
$extract = Join-Path $work 'package'
Expand-Archive -Path $zipPath -DestinationPath $extract -Force
$pkgRoot = Join-Path $extract 'ScreenRecorder-0.1.0-win-x64'
$install = Join-Path $pkgRoot 'install.ps1'
$binDir = Join-Path $pkgRoot 'bin'
Assert 'm6-10-02-package-layout' ((Test-Path $install) -and
        (Test-Path (Join-Path $binDir 'sr-recorder.exe')) -and
        (Test-Path (Join-Path $binDir 'sr-ui.exe'))) `
    '发布包含 install.ps1 + bin/sr-recorder.exe + bin/sr-ui.exe'

# 2) 无驱动组件（纯用户态安装的前提；HKLM/HVCI 下不允许装驱动）
$driverFiles = @(Get-ChildItem $extract -Recurse -File |
    Where-Object { $_.Extension -in @('.sys', '.inf', '.cat', '.vxd', '.drv') })
Assert 'm6-10-02-no-driver-payload' ($driverFiles.Count -eq 0) `
    ("包内驱动类文件数={0}（期望 0；HVCI 会阻止未签名内核驱动加载）" -f $driverFiles.Count)

# 3) install.ps1 在真实 HVCI 环境里正常执行（安装到临时目录，不改用户环境）
$installOut = Join-Path $work 'install.txt'
$target = Join-Path $work 'installed'
# install.ps1 的真实参数是 -InstallDir（见 packaging/install.ps1:14）；安装到临时目录，不改用户环境。
$installProc = Start-Process -FilePath pwsh -PassThru -WindowStyle Hidden `
    -ArgumentList @('-NoProfile', '-File', "`"$install`"", '-InstallDir', "`"$target`"", '-Quiet') `
    -RedirectStandardOutput $installOut -RedirectStandardError (Join-Path $work 'install.err')
$installExited = $installProc.WaitForExit(30000)
$installRc = if ($installExited) { $installProc.ExitCode } else { -1 }
$installText = if (Test-Path $installOut) { [System.IO.File]::ReadAllText($installOut, [System.Text.Encoding]::UTF8) } else { '' }
$installErr = if (Test-Path (Join-Path $work 'install.err')) { [System.IO.File]::ReadAllText((Join-Path $work 'install.err'), [System.Text.Encoding]::UTF8) } else { '' }
$installedExe = Join-Path $target 'bin\sr-recorder.exe'
Assert 'm6-10-02-install-runs-under-hvci' ($installRc -eq 0 -and (Test-Path $installedExe)) `
    ("HVCI 环境下 install.ps1 退出码={0}，安装产物存在={1}；stdout={2}；stderr={3}" -f `
     $installRc, (Test-Path $installedExe), ($installText.Trim() -replace "`n", ' | '),
     ($installErr.Trim() -replace "`n", ' | '))

# 4) 包内 exe 在 HVCI 环境下真实录制一段并产出可解码成片
Clear-SrProcesses -IncludeNameFallback
Start-Sleep -Milliseconds 300
$exe = Join-Path $binDir 'sr-recorder.exe'
$clip = Join-Path $work 'hvci.mkv'
$recOut = & $exe --record --seconds 3 --fps 30 --out $clip 2>&1 | Out-String
$recExit = [int](Field $recOut 'record_exit')
Assert 'm6-10-02-record-runs-under-hvci' ($recExit -eq 0 -and (Test-Path $clip) -and
        ((Get-Item $clip).Length -gt 50KB)) `
    ("包内 exe 在 HVCI 环境录制：record_exit={0} size={1} B" -f $recExit,
     $(if (Test-Path $clip) { (Get-Item $clip).Length } else { 0 }))
if (-not $ffbin) {
    Skip 'm6-10-02-decodable' 'ffprobe/ffmpeg 不可用'
} else {
    $probe = & (Join-Path $ffbin 'ffprobe.exe') -v error -show_entries format=duration `
        -show_entries stream=codec_type -of default=nw=1 $clip 2>&1 | Out-String
    $decode = & (Join-Path $ffbin 'ffmpeg.exe') -hide_banner -v error -i $clip -f null - 2>&1 | Out-String
    Assert 'm6-10-02-decodable' ([string]::IsNullOrWhiteSpace($decode) -and
            $probe -match 'codec_type=video' -and $probe -match 'codec_type=audio') `
        ("HVCI 环境产出的成片 ffmpeg 解码零错误 + video/audio 双轨；duration={0}" -f `
         (($probe -split "`n" | Where-Object { $_ -match 'duration=' } | Select-Object -First 1)))
}
# sr-ui 是 GUI 子系统进程，--version 写到父控制台而非管道；用退出码 + 日志判定可执行。
$uiExe = Join-Path $binDir 'sr-ui.exe'
$uiLog = Join-Path $work 'ui-version.log'
$uiProc = Start-Process -FilePath $uiExe -PassThru -WindowStyle Hidden `
    -ArgumentList @('--version', '--log', $uiLog)
$uiExited = $uiProc.WaitForExit(15000)
$uiRc = if ($uiExited) { $uiProc.ExitCode } else { -1 }
$uiLogText = if (Test-Path $uiLog) { [System.IO.File]::ReadAllText($uiLog, [System.Text.Encoding]::UTF8) } else { '' }
Assert 'm6-10-02-ui-runs-under-hvci' ($uiRc -eq 0 -and $uiLogText -match 'test_hooks=0') `
    ("包内 sr-ui 在 HVCI 环境启动并执行 --version：退出码={0}，日志首行={1}" -f `
     $uiRc, (($uiLogText -split "`n" | Select-Object -First 1)))

Clear-SrProcesses -IncludeNameFallback
Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP" -f $script:pass, $script:fail, $script:skip)
if ($script:fail -gt 0) { exit 1 }
exit 0
