# Screen Recorder · 卸载脚本（zip 版，零三方依赖）
#
# 用法：pwsh -File uninstall.ps1 [-InstallDir <目录>] [-Purge]
#
# 行为（docs/11 §5）：
#   * 删除程序目录、开始菜单快捷方式、HKCU 卸载登记
#   * **不删**用户成片（默认输出目录 %USERPROFILE%\Videos\ScreenRecording）
#   * -Purge 才会连用户成片一起删（默认不删）

param(
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'ScreenRecording'),
    [switch]$Purge,
    [switch]$Quiet
)

$ErrorActionPreference = 'Continue'
function Say([string]$text) { if (-not $Quiet) { Write-Host $text } }

# 1) 结束仍在运行的进程
foreach ($name in @('sr-ui', 'sr-recorder')) {
    Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

# 2) 卸载登记
try {
    Remove-Item 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\ScreenRecorder' `
        -Recurse -Force -ErrorAction SilentlyContinue
    Say "已移除卸载登记"
} catch {
    Say "警告：卸载登记移除失败（$($_.Exception.Message)）"
}

# 3) 开始菜单快捷方式
$shortcut = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\ScreenRecorder.lnk'
if (Test-Path $shortcut) {
    Remove-Item $shortcut -Force -ErrorAction SilentlyContinue
    Say "已移除开始菜单快捷方式"
}

# 4) 程序目录
if (Test-Path $InstallDir) {
    Remove-Item $InstallDir -Recurse -Force -ErrorAction SilentlyContinue
    Say "已删除程序目录 $InstallDir"
}

# 5) 用户成片：默认保留
$videos = Join-Path $env:USERPROFILE 'Videos\ScreenRecording'
if ($Purge) {
    if (Test-Path $videos) {
        Remove-Item $videos -Recurse -Force -ErrorAction SilentlyContinue
        Say "已删除用户成片 $videos（-Purge）"
    }
} elseif (Test-Path $videos) {
    Say "保留用户成片：$videos（要一并删除请加 -Purge）"
}

Say "卸载完成。"
exit 0
