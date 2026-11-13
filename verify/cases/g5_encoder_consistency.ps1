# 轮 47 · M3-05 列表与开录结果一致性（先红后绿的判据用例，验收方可独立重跑）
#   P0-1 判据（轮 47 指令 §P0-1）：
#     1) --list-encoders 里每个 available=1 的项，--encoder <id> 必须真的用上它；
#     2) 每个 available=0 的项，--encoder <id> 必须 exit 6 且原因非空；
#     3) 反向：人为让 mf-h264-hw 激活失败（--fail-encoder hardware），列表里它必须变 unavailable。
#   附加护栏：注入撤掉后可重试（失败状态不被缓存）、重复探测时本卡自身不累积句柄。
#
# 落点（产品代码，不在用例里实现）：
#   src/encode/EncoderList.{h,cpp}   真激活探测（复用录制同一条 Open + 试编码一帧）
#   src/encode/MfVideoEncoder.cpp    强制指定时不做协商/码控失败回退
#   src/recorder/main.cpp            --fail-encoder hardware|software|all、--probe-repeat <n>
#
# Usage:
#   pwsh -File verify/cases/g5_encoder_consistency.ps1 [-BuildDir build-msvc] [-Config Release]
#   pwsh -File verify/cases/g5_encoder_consistency.ps1 -RecorderPath <旧构建 sr-recorder.exe>  # 红端复跑

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release',
    [string]$RecorderPath = ''
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_encoder_consistency' -WorkDirs $script:SrForensicsWorkDirs -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($RecorderPath)) {
    $RecorderPath = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
    if (-not (Test-Path $RecorderPath)) {
        $RecorderPath = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe')
    }
}
if (-not (Test-Path $RecorderPath)) { Write-Host 'FAIL: sr-recorder.exe missing'; exit 2 }
Write-Host ("exe={0}" -f $RecorderPath)

$work = Join-Path $env:TEMP 'sr-encoder-consistency'
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

$script:seq = 0
function Invoke-Recorder {
    param([string[]]$Arguments, [int]$WaitMs = 60000)
    $script:seq++
    $tag = 'run{0}' -f $script:seq
    $out = Join-Path $work ($tag + '.out')
    $err = Join-Path $work ($tag + '.err')
    $proc = Start-Process -FilePath $RecorderPath -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $out -RedirectStandardError $err -ArgumentList $Arguments
    $finished = $proc.WaitForExit($WaitMs)
    $code = if ($finished) { $proc.ExitCode } else { -1 }
    $text = (Read-Text $out) + (Read-Text $err)
    [System.IO.File]::WriteAllText((Join-Path $work ($tag + '.cmd.txt')),
        ($RecorderPath + ' ' + ($Arguments -join ' ') + "`r`nexit=$code`r`n" + $text),
        [System.Text.UTF8Encoding]::new($false))
    return @{ Exit = $code; Text = $text; Tag = $tag }
}

function Parse-List {
    param([string]$Text)
    $items = @()
    $count = 0
    $canonical = ''
    foreach ($line in ($Text -split "`r?`n")) {
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
    return @{ Count = $count; Items = $items; Canonical = $canonical }
}

function Force-Record {
    param([string]$Id, [string[]]$Extra = @())
    $clip = Join-Path $work ('forced-' + ($Id -replace '[^a-zA-Z0-9]', '_') + '-' + $script:seq + '.mkv')
    $args = @('--record', '--seconds', '2', '--fps', '30', '--bitrate', '2000',
              '--out', "`"$clip`"", '--encoder', $Id) + $Extra
    $r = Invoke-Recorder -Arguments $args
    return @{ Exit = $r.Exit; Text = $r.Text; Clip = $clip; Tag = $r.Tag }
}

# ---------------- 0 列表结构（正向/反向的前置） ----------------
Write-Host '--- 0 sr-recorder --list-encoders ---'
$listRun = Invoke-Recorder -Arguments @('--list-encoders')
$list = Parse-List -Text $listRun.Text
Assert 'r47-list-parse' ($listRun.Exit -eq 0 -and $list.Count -ge 1 -and
        $list.Items.Count -eq $list.Count -and -not [string]::IsNullOrWhiteSpace($list.Canonical)) `
    ("exit={0} encoder_count={1} 逐项={2} 规范行长度={3}" -f `
        $listRun.Exit, $list.Count, $list.Items.Count, $list.Canonical.Length)
$hw = @($list.Items | Where-Object { $_.id -eq 'mf-h264-hw' })
Assert 'r47-list-has-hw' ($hw.Count -eq 1) ("mf-h264-hw 条目数={0}" -f $hw.Count)

# ---------------- 1 正向：available=1 的项必须真的用上 ----------------
Write-Host '--- 1 正向：每个 available=1 的项强制指定 ---'
$available = @($list.Items | Where-Object { $_.available -eq '1' })
$forwardDetail = @()
foreach ($item in $available) {
    $r = Force-Record -Id $item.id
    $used = ($r.Text -match ('encoder=' + [regex]::Escape($item.id)))
    $ok = ($r.Exit -eq 0 -and $used -and (Test-Path $r.Clip))
    $forwardDetail += ("{0}: exit={1} encoder=<{2}> 成片={3} 证据={4}.cmd.txt" -f `
        $item.id, $r.Exit,
        (($r.Text -split "`r?`n" | Where-Object { $_ -match '^\[record\] encoder=' } | Select-Object -First 1)),
        (Test-Path $r.Clip), $r.Tag)
    if (-not $ok) { $forwardOk = $false }
}
if ($null -eq $forwardOk) { $forwardOk = ($available.Count -gt 0) }
Assert 'r47-forward-available-items' ($forwardOk -and $available.Count -gt 0) `
    ("可用项={0}；逐项：{1}" -f $available.Count, ($forwardDetail -join ' | '))

# 专项护栏：强制硬编绝不允许出现「静默换成软编」。
$hwForced = Force-Record -Id 'mf-h264-hw'
Assert 'r47-no-silent-fallback' ($hwForced.Text -notmatch 'encoder=mf-h264-sw') `
    ("--encoder mf-h264-hw 的 stdout/log 是否出现软编：{0}；证据={1}.cmd.txt" -f `
        [bool]($hwForced.Text -match 'encoder=mf-h264-sw'), $hwForced.Tag)

# ---------------- 2 反向：available=0 的项必须 exit 6 + 原因 ----------------
Write-Host '--- 2 反向：每个 available=0 的项强制指定 ---'
$unavailable = @($list.Items | Where-Object { $_.available -eq '0' })
$backwardDetail = @()
$backwardOk = $true
foreach ($item in $unavailable) {
    $r = Force-Record -Id $item.id
    $hasReason = ($r.Text -match 'unavailable') -and
                 (-not [string]::IsNullOrWhiteSpace($item.reason))
    $ok = ($r.Exit -eq 6 -and $hasReason -and -not (Test-Path $r.Clip))
    if (-not $ok) { $backwardOk = $false }
    $backwardDetail += ("{0}: exit={1} 原因={2} 无成片={3} 证据={4}.cmd.txt" -f `
        $item.id, $r.Exit, $hasReason, (-not (Test-Path $r.Clip)), $r.Tag)
}
Assert 'r47-backward-unavailable-items' ($backwardOk -and $unavailable.Count -gt 0) `
    ("不可用项={0}；逐项：{1}" -f $unavailable.Count, ($backwardDetail -join ' | '))

# ---------------- 3 反向（注入）：硬件激活失败 → 列表必须不诚实不起来 ----------------
Write-Host '--- 3 注入 --fail-encoder hardware ---'
$injRun = Invoke-Recorder -Arguments @('--list-encoders', '--fail-encoder', 'hardware')
$inj = Parse-List -Text $injRun.Text
$injHw = @($inj.Items | Where-Object { $_.id -eq 'mf-h264-hw' })
$injHwUnavailable = ($injRun.Exit -eq 0 -and $injHw.Count -eq 1 -and
                     $injHw[0].available -eq '0' -and
                     -not [string]::IsNullOrWhiteSpace($injHw[0].reason))
Assert 'r47-injected-hw-unavailable' $injHwUnavailable `
    ("注入后 exit={0}；mf-h264-hw available={1} reason={2}；证据 {3}" -f `
        $injRun.Exit,
        $(if ($injHw.Count -eq 1) { $injHw[0].available } else { 'N/A' }),
        $(if ($injHw.Count -eq 1) { $injHw[0].reason } else { 'N/A' }),
        ($injRun.Tag + '.cmd.txt'))

# 注入真的走的是激活路径（不是把列表写死），且软件项不受牵连。
$injSw = @($inj.Items | Where-Object { $_.id -eq 'mf-h264-sw' })
Assert 'r47-injection-is-real-probe' ($injHw.Count -eq 1 -and
        $injHw[0].reason -match 'injected' -and $injSw.Count -eq 1 -and
        $injSw[0].available -eq '1') `
    ("硬件原因含 injected={0}；软件仍可用={1}" -f `
        [bool]($injHw.Count -eq 1 -and $injHw[0].reason -match 'injected'),
        [bool]($injSw.Count -eq 1 -and $injSw[0].available -eq '1'))

# 注入期间强制硬编同样必须 exit 6 + 原因，不是静默换软编。
$injForced = Force-Record -Id 'mf-h264-hw' -Extra @('--fail-encoder', 'hardware')
Assert 'r47-injected-forced-exit6' ($injForced.Exit -eq 6 -and
        $injForced.Text -match 'unavailable' -and -not (Test-Path $injForced.Clip)) `
    ("注入下 --encoder mf-h264-hw exit={0}，含 unavailable={1}，无成片={2}；证据 {3}.cmd.txt" -f `
        $injForced.Exit, [bool]($injForced.Text -match 'unavailable'),
        (-not (Test-Path $injForced.Clip)), $injForced.Tag)

# 撤掉注入后重跑：失败状态不得被缓存（探测失败必须能重试）。
$retryRun = Invoke-Recorder -Arguments @('--list-encoders')
$retry = Parse-List -Text $retryRun.Text
$retryHw = @($retry.Items | Where-Object { $_.id -eq 'mf-h264-hw' })
Assert 'r47-probe-retry-not-cached' ($retryRun.Exit -eq 0 -and $retryHw.Count -eq 1 -and
        $retryHw[0].reason -notmatch 'injected' -and
        ($retry.Canonical -eq $list.Canonical)) `
    ("撤注入后 reason 含 injected={0}；规范行与首轮一致={1}；证据 {2}.cmd.txt" -f `
        [bool]($retryHw.Count -eq 1 -and $retryHw[0].reason -match 'injected'),
        ($retry.Canonical -eq $list.Canonical), $retryRun.Tag)

# ---------------- 4 重试与资源护栏 ----------------
Write-Host '--- 4 重复探测（状态一致 / 自身资源不累积） ---'
function Parse-Repeat {
    param([string]$Text)
    $rows = @()
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^probe_iteration=(\d+) handles=(\d+) available_mf_sw=(\d+) available_mf_hw=(\d+)$') {
            $rows += @{ Iteration = [int]$Matches[1]; Handles = [int]$Matches[2];
                        Sw = [int]$Matches[3]; Hw = [int]$Matches[4] }
        }
    }
    return $rows
}
$repeatRun = Invoke-Recorder -Arguments @('--list-encoders', '--probe-repeat', '5')
$rows = Parse-Repeat -Text $repeatRun.Text
$statesSame = ($rows.Count -eq 5 -and
    (@($rows | ForEach-Object { "$($_.Sw)/$($_.Hw)" } | Select-Object -Unique).Count -eq 1))
Assert 'r47-repeat-state-consistent' $statesSame `
    ("重复 5 轮探测，状态串={0}；证据 {1}.cmd.txt" -f `
        (($rows | ForEach-Object { "$($_.Sw)/$($_.Hw)" }) -join ','), $repeatRun.Tag)

# 「本卡自身」资源护栏：跳过硬件 MFT 激活（注入在创建阶段就失败）后，
# 同一进程重复探测句柄必须完全不增长——证明探测器持有的 COM/MF/device 都成对释放。
$cleanRun = Invoke-Recorder -Arguments @('--list-encoders', '--probe-repeat', '5',
                                         '--fail-encoder', 'hardware')
$cleanRows = Parse-Repeat -Text $cleanRun.Text
$cleanGrowth = if ($cleanRows.Count -ge 2) {
    $cleanRows[-1].Handles - $cleanRows[0].Handles
} else { -1 }
Assert 'r47-own-resource-no-leak' ($cleanRows.Count -eq 5 -and $cleanGrowth -eq 0) `
    ("跳过硬件 MFT 激活后 5 轮句柄：{0}；增长={1}（期望 0）；证据 {2}.cmd.txt" -f `
        (($cleanRows | ForEach-Object { $_.Handles }) -join ','), $cleanGrowth, $cleanRun.Tag)

# 第三方硬件 MFT 的 ActivateObject 在本机有 +3 句柄/次的驱动侧开销（进程退出即回收）。
# 这里只做上限护栏，防止「我们自己的」资源泄漏叠加进去；原始数值如实留在产物里。
$vendorGrowth = if ($rows.Count -ge 2) { $rows[-1].Handles - $rows[0].Handles } else { -1 }
Assert 'r47-vendor-handle-bounded' ($rows.Count -eq 5 -and $vendorGrowth -ge 0 -and
        $vendorGrowth -le 4 * ($rows.Count - 1)) `
    ("真实激活 5 轮句柄：{0}；增长={1}（上限 {2}，属第三方 MFT 激活侧，见 HANDOFF 已知限制）" -f `
        (($rows | ForEach-Object { $_.Handles }) -join ','), $vendorGrowth, (4 * 4))

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL ; 产物 {2}" -f $script:pass, $script:fail, $work)
if ($script:fail -gt 0) { exit 1 }
exit 0
