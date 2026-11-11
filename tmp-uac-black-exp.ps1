# 临时实验 v4：高完整性窗口 × 中完整性 recorder → 逐后端测黑帧
$ErrorActionPreference = 'Stop'
$root = 'd:\develop\project\person\Screen Recording'
$work = Join-Path $env:TEMP 'sr-uac-exp'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$hwndFile = Join-Path $work 'hwnd.txt'
$inner = Join-Path $work 'host.ps1'
Remove-Item $hwndFile -Force -ErrorAction SilentlyContinue

@'
param([string]$Out)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$f = New-Object System.Windows.Forms.Form
$f.BackColor = [System.Drawing.Color]::Lime
$f.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::None
$f.StartPosition = [System.Windows.Forms.FormStartPosition]::Manual
$f.Location = New-Object System.Drawing.Point(100,100)
$f.ClientSize = New-Object System.Drawing.Size(800,600)
$f.TopMost = $true
$box = New-Object System.Windows.Forms.Panel
$box.BackColor = [System.Drawing.Color]::Black
$box.Size = New-Object System.Drawing.Size(64,64)
$f.Controls.Add($box)
$t = New-Object System.Windows.Forms.Timer
$t.Interval = 50
$script:x = 0
$t.Add_Tick({ $script:x = ($script:x + 7) % 700; $box.Location = New-Object System.Drawing.Point($script:x, 200) })
$f.Add_Shown({ $f.Handle.ToString() | Out-File -FilePath $Out -Encoding ascii; $t.Start() })
$t2 = New-Object System.Windows.Forms.Timer
$t2.Interval = 60000
$t2.Add_Tick({ $f.Close() })
$t2.Start()
[System.Windows.Forms.Application]::Run($f)
'@ | Set-Content -Path $inner -Encoding UTF8

Write-Host '--- 1 启动高完整性窗口 ---'
Start-Process -FilePath 'pwsh.exe' -Verb RunAs -ArgumentList @('-NoProfile', '-File', $inner, $hwndFile) -WindowStyle Hidden | Out-Null
$deadline = (Get-Date).AddSeconds(25)
while ((Get-Date) -lt $deadline -and -not (Test-Path $hwndFile)) { Start-Sleep -Milliseconds 300 }
if (-not (Test-Path $hwndFile)) { Write-Host 'FAIL: 高完整性窗口未起来'; exit 1 }
$hwnd = (Get-Content $hwndFile -Raw).Trim()
Write-Host ("elevated hwnd=" + $hwnd)

$rec = Join-Path $root 'build-msvc\bin\Release\sr-recorder.exe'

function Test-Backend {
    param([string]$Backend, [string]$Tag)
    $video = Join-Path $work ("uac-$Tag.mkv")
    Remove-Item $video -Force -ErrorAction SilentlyContinue
    Write-Host ("--- 录制 backend=" + $(if ($Backend -eq '') { '(chain)' } else { $Backend }) + " ---")
    $argList = @('--record', '--seconds', '6', '--fps', '25', '--window', $hwnd, '--fit', 'stretch', '--out', $video)
    if ($Backend -ne '') { $argList += @('--backend', $Backend) }
    $out = & $rec @argList 2>&1 | Out-String
    foreach ($k in @('backend=', 'backend_changed=', 'frames_captured=', 'capture_fps=', 'record_exit=', 'source_size=')) {
        ($out -split "`n") | Where-Object { $_ -match [regex]::Escape($k) } | ForEach-Object { Write-Host ("  " + $_.Trim()) }
    }
    if (-not (Test-Path $video)) { Write-Host '  (无成片)'; return }
    $png = Join-Path $work ("frame-$Tag.png")
    Remove-Item $png -Force -ErrorAction SilentlyContinue
    & ffmpeg -hide_banner -v error -i $video -vf "select=eq(n\,30)" -fps_mode passthrough -frames:v 1 $png 2>&1 | Out-Null
    if (-not (Test-Path $png)) { Write-Host '  (抽帧失败)'; return }
    Add-Type -AssemblyName System.Drawing
    $bmp = [System.Drawing.Bitmap]::new($png)
    $green = 0; $black = 0; $total = 0
    for ($y = 0; $y -lt $bmp.Height; $y += 4) {
        for ($x = 0; $x -lt $bmp.Width; $x += 4) {
            $c = $bmp.GetPixel($x, $y); $total++
            if ($c.G -gt 150 -and $c.R -lt 100 -and $c.B -lt 100) { $green++ }
            if ($c.R -lt 20 -and $c.G -lt 20 -and $c.B -lt 20) { $black++ }
        }
    }
    $bmp.Dispose()
    Write-Host ("  green_ratio={0:N4} black_ratio={1:N4}" -f ($green / $total), ($black / $total))
}

Test-Backend -Backend '' -Tag 'chain'
Test-Backend -Backend 'wgc' -Tag 'wgc'
Test-Backend -Backend 'printwindow' -Tag 'pw'
