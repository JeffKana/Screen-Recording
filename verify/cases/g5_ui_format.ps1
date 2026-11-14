# 轮 49 · P0-4（UI 输出扩展名跟随容器格式）
#
# 判据来自 `.codebuddy/plans/轮49指令.md` §P0-4：
#   #1 UI 侧输出名不再写死 `.mkv`：按选定格式生成扩展名（默认仍 MKV）；
#   #2 UI 录一段 → 文件名与格式一致；
#   #3 AC-10 的 `_001` 避让在 `.mp4` 上同样成立；
#   #4 `--format` 真的下发给 Recorder（`recorder_cmd ... format=mp4`），不是只改文件名。
# 附加：非法容器值被拒（`container_rejected` + INI 不变）。
#
# 用法: pwsh -File verify/cases/g5_ui_format.ps1 [-BuildDir build-msvc] [-Config Release]
#       pwsh -File verify/cases/g5_ui_format.ps1 -UiPath <轮48旧二进制>   # 红端复跑

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release',
    [string]$UiPath = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_ui_format' -WorkDirs @($script:work) -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ui = $UiPath
if ([string]::IsNullOrWhiteSpace($ui)) {
    $ui = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-ui.exe")
    if (-not (Test-Path $ui)) { $ui = Join-Path $root (Join-Path $BuildDir 'bin\sr-ui.exe') }
}
if (-not (Test-Path $ui)) { Write-Host 'FAIL: sr-ui.exe not found'; exit 2 }
Write-Host ("exe={0}" -f $ui)

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
if (-not $ffbin) { Write-Host 'BLOCKED: ffprobe not found'; exit 4 }
$ffprobe = Join-Path $ffbin 'ffprobe.exe'
$ffmpeg = Join-Path $ffbin 'ffmpeg.exe'

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class UiFormatNative {
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "FindWindowW")] public static extern IntPtr FindWindowClass(string lpClassName, IntPtr lpWindowName);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
}
'@

$script:work = Join-Path $env:TEMP 'sr-ui-format'
if (Test-Path $script:work) {
    Get-ChildItem $script:work -Recurse -Force | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force -Path $script:work | Out-Null
$script:SrForensicsWorkDirs = @($script:work)

$script:pass = 0
$script:fail = 0
function Assert {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    if ($Ok) {
        $script:pass++
        Write-Host ("  PASS {0} — {1}" -f $Name, $Detail)
    } else {
        $script:fail++
        Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail)
        [void](Save-FailureContext -Tag $Name -WorkDirs @($script:work) -Note $Detail)
    }
}
function Read-Log {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return '' }
    try {
        $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
                                          [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
        $text = $reader.ReadToEnd()
        $reader.Dispose()
        $stream.Dispose()
        return $text
    } catch {
        return ''
    }
}
function Wait-Log {
    param([string]$Path, [string]$Pattern, [int]$TimeoutMs)
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        $text = Read-Log $Path
        if ($text -match $Pattern) { return $text }
        Start-Sleep -Milliseconds 100
    }
    return (Read-Log $Path)
}
function Press-Combo {
    param([byte[]]$Keys)
    foreach ($key in $Keys) { [UiFormatNative]::keybd_event($key, 0, 0, [UIntPtr]::Zero) }
    Start-Sleep -Milliseconds 80
    for ($i = $Keys.Count - 1; $i -ge 0; $i--) {
        [UiFormatNative]::keybd_event($Keys[$i], 0, 2, [UIntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 80
}
function Test-Decodable {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return $false }
    $decode = & $ffmpeg -hide_banner -v error -i $Path -f null - 2>&1 | Out-String
    return [string]::IsNullOrWhiteSpace($decode)
}
function Get-FormatName {
    param([string]$Path)
    $raw = & $ffprobe -v error -show_entries format=format_name -of default=nw=1:nk=1 $Path 2>&1 | Out-String
    return $raw.Trim()
}
function Start-Ui {
    param([string]$Log, [string]$ConfigPath, [string]$Extra = '')
    $argList = @('--hidden', '--log', $Log)
    if ($ConfigPath) { $argList += @('--config', $ConfigPath) }
    if ($Extra) { $argList += $Extra.Split(' ', [StringSplitOptions]::RemoveEmptyEntries) }
    $proc = Start-Process -FilePath $ui -PassThru -WindowStyle Hidden -ArgumentList $argList
    Register-SrProcess $proc
    return $proc
}

$ini = Join-Path $script:work 'config.ini'
$defaultIni = Join-Path $env:LOCALAPPDATA 'sr-ui\config.ini'
$defaultBackup = Join-Path $script:work 'default-config.bak'
$hadDefaultIni = Test-Path $defaultIni
if ($hadDefaultIni) {
    Copy-Item $defaultIni $defaultBackup -Force
    Remove-Item $defaultIni -Force -ErrorAction SilentlyContinue
}

# ---------------- 1 容器设置持久化 ----------------
Write-Host '--- 1 --set-container 持久化与校验 ---'
$saveLog = Join-Path $script:work 'set-container.log'
$save = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-container', 'mp4', '--config', $ini, '--log', $saveLog)
Assert 'r49-ui-container-saved' ($save.ExitCode -eq 0 -and (Read-Log $saveLog) -match 'container_saved container=mp4') `
    ("--set-container mp4 → exit={0}，日志 container_saved={1}" -f `
        $save.ExitCode, [bool]((Read-Log $saveLog) -match 'container_saved container=mp4'))
Assert 'r49-ui-container-ini' ((Read-Log $ini) -match 'container=mp4') `
    ("INI 已写入 [output] container=mp4：{0}" -f ((Read-Log $ini) -replace "`r?`n", ' | '))

$iniBefore = Read-Log $ini
$badLog = Join-Path $script:work 'set-container-bad.log'
$bad = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-container', 'avi', '--config', $ini, '--log', $badLog)
Assert 'r49-ui-container-invalid-rejected' ($bad.ExitCode -ne 0 -and
        (Read-Log $badLog) -match 'container_rejected' -and (Read-Log $ini) -eq $iniBefore) `
    ("--set-container avi → exit={0}（期望非 0），原因已记录={1}，INI 未被改写={2}" -f `
        $bad.ExitCode, [bool]((Read-Log $badLog) -match 'container_rejected'), ((Read-Log $ini) -eq $iniBefore))

# ---------------- 2 AC-10 避让在 .mp4 上成立 ----------------
Write-Host '--- 2 UI 录制：mp4 扩展名 + _001 避让 ---'
$mp4Dir = Join-Path $script:work 'mp4_dir'
$setDirLog = Join-Path $script:work 'set-dir.log'
$null = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-output', $mp4Dir, '--config', $ini, '--log', $setDirLog)
$setTmplLog = Join-Path $script:work 'set-tmpl.log'
$null = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-template', 'ac10_{index}', '--config', $ini, '--log', $setTmplLog)

$hLog = Join-Path $script:work 'mp4-record.log'
$h = Start-Ui -Log $hLog -ConfigPath $ini
$null = Wait-Log $hLog 'app_ready' 8000
Press-Combo @(0x11, 0x12, 0x78)   # Ctrl+Alt+F9 start
$null = Wait-Log $hLog 'recording_indicator' 8000
Start-Sleep -Milliseconds 1200
Press-Combo @(0x11, 0x12, 0x7A)   # Ctrl+Alt+F11 stop
Start-Sleep -Milliseconds 1200
Press-Combo @(0x11, 0x12, 0x78)
Start-Sleep -Milliseconds 1200
Press-Combo @(0x11, 0x12, 0x7A)
Start-Sleep -Milliseconds 1500
$mainWindow = [UiFormatNative]::FindWindowClass('SrUiMainWindow', [IntPtr]::Zero)
if ($mainWindow -ne [IntPtr]::Zero) {
    [void][UiFormatNative]::PostMessage($mainWindow, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
}
$hExited = $h.WaitForExit(15000)
if (-not $hExited) { Stop-Process -Id $h.Id -Force -ErrorAction SilentlyContinue }
$hText = Read-Log $hLog

$file1 = Join-Path $mp4Dir 'ac10_001.mp4'
$file2 = Join-Path $mp4Dir 'ac10_002.mp4'
$mkvLeft = @(Get-ChildItem $mp4Dir -Filter '*.mkv' -ErrorAction SilentlyContinue).Count
$spawnCount = ([regex]::Matches($hText, 'spawn pid=')).Count
Assert 'r49-ui-mp4-two-sessions' ($spawnCount -eq 2 -and $hExited -and $h.ExitCode -eq 0) `
    ("spawn_count={0}（期望 2）exited={1} exit={2}" -f $spawnCount, $hExited, $h.ExitCode)
Assert 'r49-ui-mp4-file-001' (Test-Path $file1) ("期望产物 {0}" -f $file1)
Assert 'r49-ui-mp4-file-002-avoid' (Test-Path $file2) `
    ("AC-10 避让在 .mp4 上成立：{0}（{1}）" -f $file2, (Test-Path $file2))
Assert 'r49-ui-mp4-no-mkv' ($mkvLeft -eq 0) ("同目录 .mkv 残留={0}（期望 0）" -f $mkvLeft)
Assert 'r49-ui-mp4-format-matches' ((Get-FormatName $file1) -match 'mp4' -and (Get-FormatName $file2) -match 'mp4') `
    ("ffprobe format_name：001={0} / 002={1}" -f (Get-FormatName $file1), (Get-FormatName $file2))
Assert 'r49-ui-mp4-playable' ((Test-Decodable $file1) -and (Test-Decodable $file2)) `
    '两段 .mp4 解码零错误'
Assert 'r49-ui-format-dispatched' ($hText -match 'recorder_cmd .*format=mp4' -and
        $hText -match 'start_request out=.*\.mp4 container=mp4') `
    ("--format 真的下发了={0}；start_request 记录了容器={1}" -f `
        [bool]($hText -match 'recorder_cmd .*format=mp4'),
        [bool]($hText -match 'start_request out=.*\.mp4 container=mp4'))

# ---------------- 3 默认（MKV）零回归 ----------------
Write-Host '--- 3 默认容器仍为 MKV（零回归）---'
$mkvDir = Join-Path $script:work 'mkv_dir'
$mkvIni = Join-Path $script:work 'mkv.ini'
$setMkvDirLog = Join-Path $script:work 'set-mkv-dir.log'
$null = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-output', $mkvDir, '--config', $mkvIni, '--log', $setMkvDirLog)
$setMkvTmplLog = Join-Path $script:work 'set-mkv-tmpl.log'
$null = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-template', 'def_{index}', '--config', $mkvIni, '--log', $setMkvTmplLog)
$kLog = Join-Path $script:work 'mkv-record.log'
$k = Start-Ui -Log $kLog -ConfigPath $mkvIni -Extra '--exit-on-stop'
$null = Wait-Log $kLog 'app_ready' 8000
Press-Combo @(0x11, 0x12, 0x78)
$null = Wait-Log $kLog 'recording_indicator' 8000
Start-Sleep -Milliseconds 1200
Press-Combo @(0x11, 0x12, 0x7A)
$kExited = $k.WaitForExit(20000)
if (-not $kExited) { Stop-Process -Id $k.Id -Force -ErrorAction SilentlyContinue }
$mkvFile = Join-Path $mkvDir 'def_001.mkv'
$mp4Left = @(Get-ChildItem $mkvDir -Filter '*.mp4' -ErrorAction SilentlyContinue).Count
Assert 'r49-ui-default-mkv-name' (Test-Path $mkvFile) ("默认容器 → {0}" -f $mkvFile)
Assert 'r49-ui-default-no-mp4' ($mp4Left -eq 0) ("同目录 .mp4 残留={0}（期望 0）" -f $mp4Left)
Assert 'r49-ui-default-mkv-playable' ((Test-Decodable $mkvFile) -and
        ((Get-FormatName $mkvFile) -match 'matroska')) `
    ("ffprobe format_name={0}" -f (Get-FormatName $mkvFile))

# 恢复用户默认配置（本轮为了确定性临时移除过）。
if ($hadDefaultIni) {
    if (Test-Path $defaultBackup) {
        Copy-Item $defaultBackup $defaultIni -Force -ErrorAction SilentlyContinue
    }
} elseif (Test-Path $defaultIni) {
    Remove-Item $defaultIni -Force -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL ; 产物 {2}" -f $script:pass, $script:fail, $script:work)
if ($script:fail -gt 0) { exit 1 }
exit 0
