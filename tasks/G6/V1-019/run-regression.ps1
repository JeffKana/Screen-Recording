# 轮 46 · 回归驱动（按 `.codebuddy/plans/轮46指令.md` §通过判据 #6/#7 逐条复跑）
# 顺序执行，输出到 tasks/G6/V1-019/artifacts/r46-regression.txt；每项留 RESULT 行与退出码。
# Usage: pwsh -File tasks/G6/V1-019/run-regression.ps1

$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$out = Join-Path $PSScriptRoot 'artifacts'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$log = Join-Path $out 'r46-regression.txt'
if (Test-Path $log) { Remove-Item $log -Force }
function Log([string]$Line) {
    [System.IO.File]::AppendAllText($log, $Line + "`n", [System.Text.UTF8Encoding]::new($false))
    Write-Host $Line
}
function Run-Case {
    param([string]$Tag, [string]$Case, [string[]]$CaseArgs = @())
    Log ("=== $Tag $(Get-Date -Format 'HH:mm:ss') ===")
    $caseLog = Join-Path $out "$Tag.txt"
    $text = & pwsh -NoProfile -File (Join-Path $root $Case) @CaseArgs 2>&1 | Out-String
    $rc = $LASTEXITCODE
    [System.IO.File]::WriteAllText($caseLog, ($text + "`nEXIT=$rc`n"),
        [System.Text.UTF8Encoding]::new($false))
    foreach ($line in @($text -split "`n" | Where-Object { $_ -match 'RESULT:|FAIL |SKIP ' })) {
        Log ($line.Trim())
    }
    Log ("EXIT_${Tag}=$rc")
}

Log ("REGRESSION_START $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
Run-Case -Tag 'g5_format' -Case 'verify/cases/g5_format.ps1'
Run-Case -Tag 'g5_format_active' -Case 'verify/cases/g5_format.ps1' -CaseArgs @('-CbrScene', 'active')
Run-Case -Tag 'g5_format_selftest' -Case 'verify/cases/g5_format_selftest.ps1'
Run-Case -Tag 'g5_storage' -Case 'verify/cases/g5_storage.ps1'
Run-Case -Tag 'g5_r43_closeout' -Case 'verify/cases/g5_r43_closeout.ps1'
Run-Case -Tag 'g5_window' -Case 'verify/cases/g5_window.ps1'
Run-Case -Tag 'g5_uac' -Case 'verify/cases/g5_uac.ps1'
Run-Case -Tag 'ui_smoke_run1' -Case 'verify/cases/ui_smoke.ps1'
Run-Case -Tag 'ui_smoke_run2' -Case 'verify/cases/ui_smoke.ps1'
Run-Case -Tag 'ui_smoke_run3' -Case 'verify/cases/ui_smoke.ps1'
Run-Case -Tag 'r46_matrix_hw' -Case 'tasks/G6/V1-019/r46-matrix-hw.ps1'
Log ("REGRESSION_DONE $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
