# verify/lib/Forensics.ps1 —— 断言失败时的现场取证（轮 31 / U1）
#
# 背景：轮 30 有一次 ui_smoke 88/13（recorder 不退出 + 半截 .part），没有留下
# recorder 日志、状态序列和进程状态，事后 3 次全绿也没法定位。这里的规矩是：
# **任何一个断言失败，先把现场打包，再继续跑**；只保留第一次失败现场，避免刷屏。
#
# 产物：verify/artifacts/fail-<时间戳>-<tag>/
#   context.txt   时间/进程/命令行/工作目录清单/关键日志尾部
#   work-*/…      工作目录里的文本日志、INI 与 .part（大文件只记大小，不拷贝）
#
# 用法（用例脚本）：
#   . (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
#   $script:SrForensicsWorkDirs = @($work)
#   Assert 'xxx' $ok $detail   # 失败时自动取证

$script:SrForensicsSaved = $false
$script:SrForensicsDir = ''
$script:SrForensicsWorkDirs = @()

function Save-FailureContext {
    param(
        [string]$Tag = 'unknown',
        [string[]]$WorkDirs = @(),
        [string]$Note = ''
    )
    if ($script:SrForensicsSaved) { return $script:SrForensicsDir }
    $script:SrForensicsSaved = $true
    try {
        $root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $safe = ($Tag -replace '[^\w\-\.]', '_')
        $dir = Join-Path $root ("verify/artifacts/fail-{0}-{1}" -f $stamp, $safe)
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        $script:SrForensicsDir = $dir

        $lines = New-Object System.Collections.Generic.List[string]
        $lines.Add("time=$(Get-Date -Format o)")
        $lines.Add("tag=$Tag")
        $lines.Add("note=$Note")
        $lines.Add("cwd=$($PWD.Path)")
        $lines.Add("ps=$($PSVersionTable.PSVersion)")
        $lines.Add('')
        $lines.Add('## processes')
        foreach ($p in @(Get-Process -Name sr-ui, sr-recorder -ErrorAction SilentlyContinue)) {
            $info = 'pid={0} name={1} start={2} ws_mb={3:N1} cpu={4:N1} handles={5} responding={6}' -f `
                $p.Id, $p.ProcessName, $p.StartTime.ToString('o'), ($p.WorkingSet64 / 1MB), $p.CPU,
                $p.HandleCount, $p.Responding
            $lines.Add($info)
        }
        $lines.Add('')
        $lines.Add('## command lines')
        $procs = Get-CimInstance Win32_Process -Filter "Name='sr-ui.exe' OR Name='sr-recorder.exe'" `
            -ErrorAction SilentlyContinue
        foreach ($c in @($procs)) {
            $lines.Add(('pid={0} cmd={1}' -f $c.ProcessId, $c.CommandLine))
        }
        $lines.Add('')
        $lines.Add('## leftovers (.part / .tmp in temp work dirs)')
        # 只扫我们自己的 sr-* 工作目录：整个 TEMP 递归太慢，取证本身不能拖垮用例。
        foreach ($workRoot in @(Get-ChildItem -Path $env:TEMP -Directory -Filter 'sr-*' -ErrorAction SilentlyContinue)) {
            foreach ($file in @(Get-ChildItem -Path $workRoot.FullName -Include '*.part', '*.tmp' -File -Recurse -ErrorAction SilentlyContinue)) {
                $lines.Add(('{0} bytes={1} mtime={2:o}' -f $file.FullName, $file.Length, $file.LastWriteTime))
            }
        }

        $index = 0
        foreach ($work in $WorkDirs) {
            if ([string]::IsNullOrEmpty($work) -or -not (Test-Path $work)) { continue }
            $index++
            $dest = Join-Path $dir ("work-{0}" -f $index)
            New-Item -ItemType Directory -Force -Path $dest | Out-Null
            $lines.Add('')
            $lines.Add("## work dir $index = $work")
            foreach ($file in @(Get-ChildItem $work -File -Recurse -ErrorAction SilentlyContinue)) {
                $lines.Add(('{0} bytes={1}' -f $file.FullName.Substring($work.Length).TrimStart('\'), $file.Length))
                $isText = $file.Extension -in '.log', '.txt', '.ini'
                $isPart = $file.Extension -in '.part'
                if (($isText -and $file.Length -le 2MB) -or ($isPart -and $file.Length -le 64MB)) {
                    try {
                        Copy-Item $file.FullName (Join-Path $dest $file.Name) -Force -ErrorAction Stop
                    } catch { }
                }
            }
            foreach ($log in @(Get-ChildItem $work -Filter '*.log' -File -Recurse -ErrorAction SilentlyContinue)) {
                $lines.Add('')
                $lines.Add(("### tail {0}" -f $log.Name))
                $text = ''
                try { $text = [System.IO.File]::ReadAllText($log.FullName) } catch { }
                $tail = ($text -split "`r?`n") | Select-Object -Last 120
                foreach ($row in $tail) { $lines.Add($row) }
            }
        }

        [System.IO.File]::WriteAllText((Join-Path $dir 'context.txt'), ($lines -join "`r`n"),
            [System.Text.UTF8Encoding]::new($false))
        Write-Host ("  [forensics] 现场已保存: {0}" -f $dir)
    } catch {
        Write-Host ("  [forensics] 取证失败（不影响判定）: {0}" -f $_)
    }
    return $script:SrForensicsDir
}
