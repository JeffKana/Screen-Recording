# 轮 37 · M3-05 编码器列表（实际可用 + 不可用原因）：
#   #1 界面显示实际可用列表（≥1 项，且含 mf-h264-sw）
#   #2 不可用项带**真实探测**的原因（不是写死一句话）
#   附加：
#   - 强行指定不可用编码器 → 开录失败并给出原因（复用既有 exit 6 路径）
#   - 强行指定可用编码器 → 录制成功且日志 encoder= 与所选一致
#   - 日志 `encoders=[...]`（recorder）与 UI 日志逐字一致；UI 列表条目数与列表项数一致
#
# 落点（产品代码，不在用例里实现）：
#   src/encode/EncoderList.{h,cpp}：枚举 + 探测 + 规范单行序列化
#   src/recorder/main.cpp：--list-encoders / --encoder <id>
#   src/encode/MfVideoEncoder.cpp：显式指定时不做「硬编→软编」回退
#   src/app/main.cpp：设置页的可用编码器列表（只读）
#
# Usage: pwsh -File verify/cases/g5_encoder_list.ps1 [-BuildDir build-msvc] [-Config Release]

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_encoder_list' -WorkDirs $script:SrForensicsWorkDirs -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
$ui = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-ui.exe")
if (-not (Test-Path $rec)) { $rec = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $ui)) { $ui = Join-Path $root (Join-Path $BuildDir 'bin\sr-ui.exe') }
if (-not (Test-Path $rec) -or -not (Test-Path $ui)) { Write-Host 'FAIL: binaries missing'; exit 2 }

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class EncoderListNative {
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "FindWindowW")]
    public static extern IntPtr FindWindowClass(string lpClassName, IntPtr lpWindowName);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hDlg, int nIDDlgItem);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    public const uint LB_GETCOUNT = 0x018B;
    public const uint WM_CLOSE = 0x0010;
}
'@

$work = Join-Path $env:TEMP 'sr-encoderlist'
if (Test-Path $work) { Get-ChildItem $work -Recurse -Force | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force -Path $work | Out-Null
$script:SrForensicsWorkDirs = @($work)

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
        [void](Save-FailureContext -Tag $Name -WorkDirs @($work) -Note $Detail)
    }
}

function Read-Text {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return '' }
    return [System.IO.File]::ReadAllText($Path)
}

# UI 还在跑时日志文件被独占，直接 ReadAllText 会抛「being used by another process」。
function Read-TextShared {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return '' }
    try {
        $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
                                         [System.IO.FileAccess]::Read,
                                         [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
        try {
            $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
            return $reader.ReadToEnd()
        } finally {
            if ($reader) { $reader.Dispose() }
            $stream.Dispose()
        }
    } catch {
        return ''
    }
}

# ---------------- 1 编码器列表 ----------------
Write-Host '--- 1 sr-recorder --list-encoders ---'
$listOut = Join-Path $work 'list.out'
$listErr = Join-Path $work 'list.err'
$listLog = Join-Path $work 'list.log'
$listProc = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden -RedirectStandardOutput $listOut `
    -RedirectStandardError $listErr -ArgumentList @('--list-encoders', '--log-file', "`"$listLog`"")
$finished = $listProc.WaitForExit(30000)
$listExit = if ($finished) { $listProc.ExitCode } else { -1 }
$listText = Read-Text $listOut
Assert 'm3-05-list-exit' ($listExit -eq 0) ("--list-encoders exit={0}" -f $listExit)

$canonical = ''
$count = 0
$items = @()
foreach ($line in ($listText -split "`r?`n")) {
    if ($line -match '^encoder_count=(\d+)$') { $count = [int]$Matches[1] }
    elseif ($line -match '^encoders=\[(.*)\]$') { $canonical = $Matches[1] }
    elseif ($line -match '^encoder=(.*)$') {
        $fields = @{}
        foreach ($field in ($Matches[1] -split "`t")) {
            $pos = $field.IndexOf('=')
            if ($pos -gt 0) { $fields[$field.Substring(0, $pos)] = $field.Substring($pos + 1) }
        }
        $items += $fields
    }
}
Assert 'm3-05-list-count' ($count -ge 1 -and $items.Count -eq $count) `
    ("encoder_count={0} 逐项行数={1}（期望 ≥1 且相等）" -f $count, $items.Count)

$sw = @($items | Where-Object { $_.id -eq 'mf-h264-sw' })
Assert 'm3-05-has-mf-h264-sw' ($sw.Count -eq 1) `
    ("列表 id：{0}" -f (($items | ForEach-Object { $_.id }) -join ','))

$unavailable = @($items | Where-Object { $_.available -eq '0' })
$badReason = @($unavailable | Where-Object {
    [string]::IsNullOrWhiteSpace($_.reason) -or
    ($_.reason -notmatch '\.dll|SDK|驱动|MFT')
})
Assert 'm3-05-unavailable-reason' ($unavailable.Count -ge 1 -and $badReason.Count -eq 0) `
    ("不可用项={0}；原因样例：{1}" -f $unavailable.Count,
        (($unavailable | Select-Object -First 1 | ForEach-Object { $_.reason }) -join ''))

$vendorAvailable = @($items | Where-Object {
    $_.id -ne 'mf-h264-sw' -and $_.id -ne 'mf-h264-hw' -and $_.available -eq '1'
})
Assert 'm3-05-vendor-never-available' ($vendorAvailable.Count -eq 0) `
    ("厂商项被标成可用的个数={0}（期望 0：本产品未集成厂商 SDK）" -f $vendorAvailable.Count)

Assert 'm3-05-canonical-line' (-not [string]::IsNullOrWhiteSpace($canonical)) `
    ("规范单行长度={0}" -f $canonical.Length)

# ---------------- 2 强行指定不可用编码器 → exit 6 ----------------
Write-Host '--- 2 强行指定不可用编码器 ---'
$forcedId = if ($unavailable.Count -ge 1) { $unavailable[0].id } else { 'nvenc-h264' }
$forcedOut = Join-Path $work 'forced.out'
$forcedLog = Join-Path $work 'forced.log'
$forcedErr = Join-Path $work 'forced.err'
$clip = Join-Path $work 'forced.mkv'
$forcedProc = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden -RedirectStandardOutput $forcedOut `
    -RedirectStandardError $forcedErr `
    -ArgumentList @('--record', '--seconds', '3', '--fps', '30', '--bitrate', '2000',
                    '--out', "`"$clip`"", '--encoder', $forcedId,
                    '--log-level', 'info', '--log-file', "`"$forcedLog`"")
$finished = $forcedProc.WaitForExit(60000)
$forcedExit = if ($finished) { $forcedProc.ExitCode } else { -1 }
$forcedText = (Read-Text $forcedOut) + (Read-Text $forcedLog)
Assert 'm3-05-force-unavailable-exit6' ($forcedExit -eq 6) `
    ("--encoder {0} 的退出码={1}（期望 6：复用既有「encoder open failed」路径）" -f $forcedId, $forcedExit)
$reasonHit = ($forcedText -match [regex]::Escape($forcedId)) -and ($forcedText -match 'unavailable')
Assert 'm3-05-force-unavailable-reason' ($reasonHit) `
    ("日志含编码器 id 与 unavailable 原因：{0}" -f (($forcedText -split "`r?`n" | Where-Object { $_ -match 'encoder' } | Select-Object -First 1)))
Assert 'm3-05-force-unavailable-no-product' (-not (Test-Path $clip)) `
    ("失败后是否留下成品：{0}（期望 False）" -f (Test-Path $clip))

# ---------------- 3 强行指定可用编码器 → 录制成功 ----------------
Write-Host '--- 3 强行指定可用编码器 mf-h264-sw ---'
$okOut = Join-Path $work 'ok.out'
$okLog = Join-Path $work 'ok.log'
$okClip = Join-Path $work 'ok.mkv'
$okProc = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden -RedirectStandardOutput $okOut `
    -RedirectStandardError (Join-Path $work 'ok.err') `
    -ArgumentList @('--record', '--seconds', '3', '--fps', '30', '--bitrate', '2000',
                    '--out', "`"$okClip`"", '--encoder', 'mf-h264-sw',
                    '--log-level', 'info', '--log-file', "`"$okLog`"")
$finished = $okProc.WaitForExit(90000)
$okExit = if ($finished) { $okProc.ExitCode } else { -1 }
$okText = (Read-Text $okOut) + (Read-Text $okLog)
Assert 'm3-05-force-available-exit0' ($okExit -eq 0) ("--encoder mf-h264-sw 退出码={0}" -f $okExit)
Assert 'm3-05-force-available-log' ($okText -match 'encoder=mf-h264-sw') `
    ("日志 encoder= 行：{0}" -f (($okText -split "`r?`n" | Where-Object { $_ -match 'encoder=' } | Select-Object -First 1)))
Assert 'm3-05-force-available-product' (Test-Path $okClip) `
    ("成片存在：{0}" -f (Test-Path $okClip))

# ---------------- 4 UI 列表与日志一致 ----------------
Write-Host '--- 4 UI 设置页的编码器列表 ---'
$uiLog = Join-Path $work 'ui.log'
$uiIni = Join-Path $work 'config.ini'
[System.IO.File]::WriteAllText($uiIni,
    "[hotkeys]`r`nstart=Ctrl+Alt+F9`r`npause=Ctrl+Alt+F10`r`nstop=Ctrl+Alt+F11`r`n" +
    "[output]`r`ndir=$work`r`n",
    [System.Text.UTF8Encoding]::new($false))
$uiProc = Start-Process -FilePath $ui -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $work 'ui.out') -RedirectStandardError (Join-Path $work 'ui.err') `
    -ArgumentList @('--hidden', '--log', "`"$uiLog`"", '--config', "`"$uiIni`"", '--show-settings')
Register-SrProcess $uiProc
$deadline = (Get-Date).AddSeconds(15)
$settings = [IntPtr]::Zero
while ((Get-Date) -lt $deadline) {
    $settings = [EncoderListNative]::FindWindowClass('SrUiSettingsWindow', [IntPtr]::Zero)
    if ($settings -ne [IntPtr]::Zero) { break }
    Start-Sleep -Milliseconds 100
}
Assert 'm3-05-ui-settings-window' ($settings -ne [IntPtr]::Zero) ("设置窗口 hwnd={0}" -f $settings)

if ($settings -ne [IntPtr]::Zero) {
    $listBox = [EncoderListNative]::GetDlgItem($settings, 107)
    Assert 'm3-05-ui-listbox-exists' ($listBox -ne [IntPtr]::Zero) ("列表控件 hwnd={0}" -f $listBox)
    if ($listBox -ne [IntPtr]::Zero) {
        $uiCount = [int][EncoderListNative]::SendMessage($listBox, [EncoderListNative]::LB_GETCOUNT, [IntPtr]::Zero, [IntPtr]::Zero)
        Assert 'm3-05-ui-listbox-count' ($uiCount -eq $count) `
            ("UI 列表条目数={0}，recorder 列表项数={1}（期望相等）" -f $uiCount, $count)
    }
    $uiLogText = Read-TextShared $uiLog
    $uiCanonical = ''
    $uiRows = -1
    foreach ($line in ($uiLogText -split "`r?`n")) {
        if ($line -match 'encoders=\[(.*)\]') { $uiCanonical = $Matches[1] }
        elseif ($line -match 'encoder_rows=(\d+)') { $uiRows = [int]$Matches[1] }
    }
    Assert 'm3-05-ui-log-same-line' (-not [string]::IsNullOrWhiteSpace($uiCanonical) -and $uiCanonical -eq $canonical) `
        ("UI 日志行与 recorder 规范单行逐字一致：{0}" -f ($uiCanonical -eq $canonical))
    Assert 'm3-05-ui-log-rows' ($uiRows -eq $count) `
        ("UI 日志 encoder_rows={0}，recorder 列表项数={1}" -f $uiRows, $count)
    [void][EncoderListNative]::PostMessage($settings, [EncoderListNative]::WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
}
if ($uiProc -and -not $uiProc.HasExited) { Stop-SrProcess $uiProc }
Unregister-SrProcess $uiProc

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL ; 产物 {2}" -f $script:pass, $script:fail, $work)
if ($script:fail -gt 0) { exit 1 }
exit 0
