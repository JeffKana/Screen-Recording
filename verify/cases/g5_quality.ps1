# 轮 33 · R3：D-53 的「画质上限 70」不能只有码率数据，需要 70/85 的画质对照。
#
# 方法（可复现，不目测）：
#   1. 用 ffmpeg 生成一张 1920×1080 的高细节静态图，全屏置顶显示；
#   2. 同一画面分别以 CBR 6000 + quality 70 / 85 各录 8s；
#   3. 各抽第 3s 的一帧，与**源图**算 PSNR / SSIM（源图是唯一的参考，不拿两份编码互比）；
#   4. 同时记录逐秒码率与 filler 占比，说明「同为 6000kbps 时画质档位是否真的换了东西」。
#
# Usage: pwsh -File verify/cases/g5_quality.ps1

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release',
    [int]$Seconds = 8
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_quality' -WorkDirs $script:SrForensicsWorkDirs -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    if ($script:Viewer) { Stop-Process -Id $script:Viewer.Id -Force -ErrorAction SilentlyContinue }
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $rec)) { $rec = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $rec)) { Write-Host "FAIL: missing $rec"; exit 2 }
$ffbin = Split-Path -Parent (Get-Command ffmpeg).Source
$ffmpeg = Join-Path $ffbin 'ffmpeg.exe'

$work = Join-Path $env:TEMP 'sr-quality'
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
function Field {
    param([string]$Text, [string]$Key)
    if ($Text -match "(?m)^$Key=(.*)$") { return $Matches[1].Trim() }
    return ''
}

# 源图：testsrc2 的噪声/彩条细节足够多，能区分画质档位。
$sourceImage = Join-Path $work 'source.png'
& $ffmpeg -hide_banner -loglevel error -f lavfi -i 'testsrc2=size=1920x1080:rate=1' `
    -frames:v 1 -y $sourceImage 2>&1 | Out-Null
Assert 'quality-source-image' (Test-Path $sourceImage) ("source={0}" -f $sourceImage)

$viewerScript = Join-Path $work 'viewer.ps1'
@'
param([string]$Image)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$form = New-Object System.Windows.Forms.Form
$form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::None
$form.WindowState = [System.Windows.Forms.FormWindowState]::Maximized
$form.TopMost = $true
$form.ShowInTaskbar = $false
$picture = New-Object System.Windows.Forms.PictureBox
$picture.Dock = [System.Windows.Forms.DockStyle]::Fill
$picture.SizeMode = [System.Windows.Forms.PictureBoxSizeMode]::StretchImage
$picture.Image = [System.Drawing.Image]::FromFile($Image)
$form.Controls.Add($picture)
[System.Windows.Forms.Application]::Run($form)
'@ | Set-Content -Path $viewerScript -Encoding UTF8

$script:Viewer = Start-Process -FilePath (Get-Command pwsh).Source -PassThru -WindowStyle Hidden `
    -ArgumentList @('-NoProfile', '-File', $viewerScript, '-Image', $sourceImage)
Start-Sleep -Seconds 3

function Record-Quality {
    param([int]$Quality, [string]$Tag)
    $out = Join-Path $work "$Tag.mkv"
    $stdout = & $rec --record --seconds "$Seconds" --fps 30 --bitrate 6000 --quality "$Quality" `
        --out "$out" --log-level warn 2>&1 | Out-String
    return [pscustomobject]@{ Text = $stdout; Path = $out }
}

$q70 = Record-Quality -Quality 70 -Tag 'q70'
$q85 = Record-Quality -Quality 85 -Tag 'q85'
Stop-Process -Id $script:Viewer.Id -Force -ErrorAction SilentlyContinue
$script:Viewer = $null

function Get-Frame {
    param([string]$Video, [string]$Name)
    $path = Join-Path $work $Name
    & $ffmpeg -hide_banner -loglevel error -ss 3 -i $Video -frames:v 1 -y $path 2>&1 | Out-Null
    return $path
}
$frame70 = Get-Frame -Video $q70.Path -Name 'q70.png'
$frame85 = Get-Frame -Video $q85.Path -Name 'q85.png'
Assert 'quality-frames-extracted' ((Test-Path $frame70) -and (Test-Path $frame85)) '两份成片各抽到第 3s 帧'

function Get-PsnrSsim {
    param([string]$Frame, [string]$Reference)
    $filter = "[0:v]scale=1920:1080,format=yuv420p[a];[1:v]scale=1920:1080,format=yuv420p[b];[a][b]psnr"
    $psnrOut = & $ffmpeg -hide_banner -i $Frame -i $Reference -lavfi $filter -f null - 2>&1 | Out-String
    $psnr = -1.0
    if ($psnrOut -match 'average:([\d\.]+|inf)') {
        $psnr = if ($Matches[1] -eq 'inf') { 100.0 } else { [double]$Matches[1] }
    }
    $ssimFilter = "[0:v]scale=1920:1080,format=yuv420p[a];[1:v]scale=1920:1080,format=yuv420p[b];[a][b]ssim"
    $ssimOut = & $ffmpeg -hide_banner -i $Frame -i $Reference -lavfi $ssimFilter -f null - 2>&1 | Out-String
    $ssim = -1.0
    if ($ssimOut -match 'All:([\d\.]+)') { $ssim = [double]$Matches[1] }
    return [pscustomobject]@{ Psnr = $psnr; Ssim = $ssim }
}

$psnr70 = Get-PsnrSsim -Frame $frame70 -Reference $sourceImage
$psnr85 = Get-PsnrSsim -Frame $frame85 -Reference $sourceImage
Assert 'quality-psnr-measured' (($psnr70.Psnr -gt 0) -and ($psnr85.Psnr -gt 0)) `
    ("q70={0:N2}dB q85={1:N2}dB（对源图）" -f $psnr70.Psnr, $psnr85.Psnr)
Assert 'quality-ssim-measured' (($psnr70.Ssim -gt 0) -and ($psnr85.Ssim -gt 0)) `
    ("q70={0:N4} q85={1:N4}（对源图）" -f $psnr70.Ssim, $psnr85.Ssim)
# 画质上限更高不应该更差；0.3dB 容差用于同帧捕获的噪声。
Assert 'quality-cap85-not-worse' ($psnr85.Psnr -ge ($psnr70.Psnr - 0.3)) `
    ("ΔPSNR={0:N2}dB（判据 ≥ -0.3dB）" -f ($psnr85.Psnr - $psnr70.Psnr))

$filler70 = [long](Field $q70.Text 'cbr_filler_bytes')
$filler85 = [long](Field $q85.Text 'cbr_filler_bytes')
# 回读值只证明「属性被接受」，不证明「画质真的变了」——后者看 PSNR/SSIM。
$readback70 = [int](Field $q70.Text 'rate_control_quality')
$readback85 = [int](Field $q85.Text 'rate_control_quality')
$summary = [pscustomobject]@{
    quality_70   = $psnr70
    quality_85   = $psnr85
    readback_70  = $readback70
    readback_85  = $readback85
    filler_70    = $filler70
    filler_85    = $filler85
    file_70      = (Get-Item $q70.Path).Length
    file_85      = (Get-Item $q85.Path).Length
    pass         = $script:pass
    fail         = $script:fail
    work         = $work
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $work 'summary.json') -Encoding UTF8
Write-Host ''
Write-Host ("质量对照：q70 PSNR {0:N2}dB / SSIM {1:N4}（回读 {4}）；q85 PSNR {2:N2}dB / SSIM {3:N4}（回读 {5}）" -f `
    $psnr70.Psnr, $psnr70.Ssim, $psnr85.Psnr, $psnr85.Ssim, $readback70, $readback85)
Write-Host ("filler：q70 {0:N0} 字节 / q85 {1:N0} 字节" -f $filler70, $filler85)
Write-Host ("RESULT: {0} PASS / {1} FAIL ; 产物 {2}" -f $script:pass, $script:fail, $work)
if ($script:fail -gt 0) { exit 1 }
exit 0
