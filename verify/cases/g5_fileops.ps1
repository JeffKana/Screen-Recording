# 轮 33 · M5-07 文件操作：
#   #1 重命名后磁盘文件名同步变更（并且列表刷新到新名字）
#   #2 删除进回收站而非直接抹除（$Recycle.Bin 的 $I 记录可查）
#   #3 批量操作有二次确认（confirm=0 不改磁盘；confirm=1 才删）
#   附加：正在录制的 .part 不得被删除/重命名
#
# 操作入口与产品菜单共用同一条实现（PerformRename / PerformDelete → FileOps.cpp），
# 不是脚本自己调 MoveFile/Remove-Item。
#
# Usage: pwsh -File verify/cases/g5_fileops.ps1

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_fileops' -WorkDirs $script:SrForensicsWorkDirs -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ui = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-ui.exe")
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $ui)) { $ui = Join-Path $root (Join-Path $BuildDir 'bin\sr-ui.exe') }
if (-not (Test-Path $rec)) { $rec = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $ui) -or -not (Test-Path $rec)) { Write-Host 'FAIL: binaries missing'; exit 2 }

$work = Join-Path $env:TEMP 'sr-fileops'
if (Test-Path $work) { Get-ChildItem $work -Recurse -Force | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force -Path $work | Out-Null
$script:SrForensicsWorkDirs = @($work)
$ini = Join-Path $work 'config.ini'
[System.IO.File]::WriteAllText($ini,
    "[hotkeys]`r`nstart=Ctrl+Alt+F9`r`npause=Ctrl+Alt+F10`r`nstop=Ctrl+Alt+F11`r`n" +
    "[output]`r`ndir=$work`r`n",
    [System.Text.UTF8Encoding]::new($false))

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

function New-Clip {
    param([string]$Name, [int]$Seconds = 2)
    $path = Join-Path $work $Name
    $proc = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden `
        -ArgumentList @('--record', '--seconds', "$Seconds", '--fps', '30', '--bitrate', '2000',
                        '--out', "`"$path`"", '--log-level', 'warn')
    Register-SrProcess $proc
    [void]$proc.WaitForExit(60000)
    Unregister-SrProcess $proc
    return $path
}

function Invoke-ListOp {
    param([string]$Tag, [string]$Op, [string]$From, [string]$To = '', [string]$Confirm = 'yes')
    $log = Join-Path $work "$Tag.log"
    $argList = @('--hidden', '--show-list', '--config', $ini, '--log', $log, '--exit-after-ms', '7000',
                 '--list-op', $Op, '--list-from', $From, '--list-confirm', $Confirm)
    if ($To) { $argList += @('--list-to', $To) }
    $proc = Start-Process -FilePath $ui -PassThru -WindowStyle Hidden -ArgumentList $argList
    Register-SrProcess $proc
    [void]$proc.WaitForExit(20000)
    Unregister-SrProcess $proc
    return (Read-Text $log)
}

# 回收站 $I 记录：8B 头 + 8B 大小 + 8B 时间 + 4B 路径长度 + UTF-16 原路径。
# 注意：$Recycle.Bin 及其 SID 子目录都是 hidden/system，Get-ChildItem 必须加 -Force。
function Test-InRecycleBin {
    param([string]$FileName, [int]$TimeoutMs = 5000)
    $needle = $FileName.ToLowerInvariant()
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    do {
        foreach ($dir in (Get-ChildItem 'C:\$Recycle.Bin' -Directory -Force -ErrorAction SilentlyContinue)) {
            foreach ($item in (Get-ChildItem $dir.FullName -Filter '$I*' -File -Force -ErrorAction SilentlyContinue)) {
                try {
                    $bytes = [System.IO.File]::ReadAllBytes($item.FullName)
                    if ($bytes.Length -le 28) { continue }
                    $text = [System.Text.Encoding]::Unicode.GetString($bytes, 28, $bytes.Length - 28)
                    if ($text.ToLowerInvariant().Contains($needle)) { return $true }
                } catch {
                    continue
                }
            }
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    return $false
}

Get-Process sr-ui, sr-recorder -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

# ---------------- 1 重命名 ----------------
Write-Host '--- M5-07#1 重命名：磁盘 + 列表同步 ---'
$alpha = New-Clip 'alpha.mkv'
Assert 'rename-setup' (Test-Path $alpha) 'alpha.mkv 已生成'
$renameLog = Invoke-ListOp -Tag 'rename' -Op 'rename' -From 'alpha.mkv' -To 'beta.mkv'
$beta = Join-Path $work 'beta.mkv'
Assert 'rename-disk-sync' ((Test-Path $beta) -and -not (Test-Path $alpha)) `
    ("beta_exists={0} alpha_gone={1}" -f (Test-Path $beta), (-not (Test-Path $alpha)))
Assert 'rename-log' ($renameLog -match 'file_op op=rename ok=1') '日志 file_op op=rename ok=1'
$afterRename = if ($renameLog -match '(?s)file_op op=rename ok=1.*$') { $Matches[0] } else { '' }
Assert 'rename-list-sync' ($afterRename -match 'list_entry .*name=beta\.mkv' -and
                           $afterRename -notmatch 'list_entry .*name=alpha\.mkv') `
    '列表刷新后只有 beta.mkv'

# ---------------- 2 删除到回收站 ----------------
Write-Host '--- M5-07#2 删除进回收站 ---'
# 名字带时间戳：回收站里可能已有上一轮的 gamma.mkv，同名会让断言假通过。
$runId = Get-Date -Format 'HHmmss'
$gammaName = "gamma-$runId.mkv"
$gamma = New-Clip $gammaName
$deleteLog = Invoke-ListOp -Tag 'delete' -Op 'delete' -From $gammaName -Confirm 'yes'
Assert 'delete-disk' (-not (Test-Path $gamma) -and $deleteLog -match 'file_op op=delete ok=1 .*recycle=1') `
    ("gone={0} recycle_log={1}" -f (-not (Test-Path $gamma)), ($deleteLog -match 'recycle=1'))
Assert 'delete-recycle-bin' (Test-InRecycleBin $gammaName) "回收站 `$I 记录含 $gammaName"

# ---------------- 3 批量二次确认 ----------------
Write-Host '--- M5-07#3 批量删除二次确认 ---'
$eps1 = New-Clip 'eps1.mkv'
$eps2 = New-Clip 'eps2.mkv'
$cancelLog = Invoke-ListOp -Tag 'batch-cancel' -Op 'delete' -From 'eps1.mkv;eps2.mkv' -Confirm 'no'
Assert 'batch-confirm-cancel' ((Test-Path $eps1) -and (Test-Path $eps2) -and
                               $cancelLog -match 'file_op op=delete ok=0 count=2 confirm=0') `
    'confirm=0 时两份文件都还在'
$batchLog = Invoke-ListOp -Tag 'batch' -Op 'delete' -From 'eps1.mkv;eps2.mkv' -Confirm 'yes'
Assert 'batch-confirm-yes' ((-not (Test-Path $eps1)) -and (-not (Test-Path $eps2)) -and
                            $batchLog -match 'file_op op=delete ok=1 count=2 confirm=1') `
    'confirm=1 时两份文件都进回收站'

# ---------------- 4 .part 保护 ----------------
Write-Host '--- 附加：正在录制的 .part 不得删除 ---'
$part = Join-Path $work 'live.mkv.part'
[System.IO.File]::WriteAllBytes($part, [byte[]](1, 2, 3, 4))
$partLog = Invoke-ListOp -Tag 'part' -Op 'delete' -From 'live.mkv.part' -Confirm 'yes'
Assert 'part-protected' ((Test-Path $part) -and $partLog -match 'file_op op=delete ok=0' -and
                         $partLog -match '临时文件') 'live.mkv.part 未被删除'

$summary = [pscustomobject]@{
    pass = $script:pass
    fail = $script:fail
    work = $work
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $work 'summary.json') -Encoding UTF8
Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL ; 产物 {2}" -f $script:pass, $script:fail, $work)
if ($script:fail -gt 0) { exit 1 }
exit 0
