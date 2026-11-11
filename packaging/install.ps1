# Screen Recorder · 安装脚本（zip 版，零三方依赖）
#
# 用法：
#   pwsh -File install.ps1                 # 安装到 %LOCALAPPDATA%\ScreenRecording
#   pwsh -File install.ps1 -InstallDir D:\Apps\ScreenRecording
#
# 行为（docs/11 §5）：
#   * 免管理员、逐用户安装（%LOCALAPPDATA%）
#   * 写 HKCU 卸载登记 → 「应用和功能」里能看到
#   * 建开始菜单快捷方式
#   * **不删**用户成片（默认输出目录 %USERPROFILE%\Videos\ScreenRecording）

param(
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'ScreenRecording'),
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $root 'bin'
if (-not (Test-Path $binDir)) {
    Write-Host "FAIL: 包内缺少 bin 目录（$binDir）"
    exit 2
}

function Say([string]$text) { if (-not $Quiet) { Write-Host $text } }

$target = Join-Path $InstallDir 'bin'
New-Item -ItemType Directory -Force -Path $target | Out-Null
$copied = @()
foreach ($exe in @('sr-ui.exe', 'sr-recorder.exe')) {
    $source = Join-Path $binDir $exe
    if (-not (Test-Path $source)) {
        Write-Host "FAIL: 包内缺少 $exe"
        exit 2
    }
    Copy-Item $source (Join-Path $target $exe) -Force
    $copied += $exe
}
if (Test-Path (Join-Path $root 'VERSION.txt')) {
    Copy-Item (Join-Path $root 'VERSION.txt') (Join-Path $InstallDir 'VERSION.txt') -Force
}
if (Test-Path (Join-Path $root 'README.txt')) {
    Copy-Item (Join-Path $root 'README.txt') (Join-Path $InstallDir 'README.txt') -Force
}
Say "已安装到 $InstallDir（$($copied -join ', ')）"

# 开始菜单快捷方式（WScript.Shell 是系统自带 COM，不引三方依赖）
try {
    $programs = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
    New-Item -ItemType Directory -Force -Path $programs | Out-Null
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut((Join-Path $programs 'ScreenRecorder.lnk'))
    $shortcut.TargetPath = Join-Path $target 'sr-ui.exe'
    $shortcut.WorkingDirectory = $InstallDir
    $shortcut.Description = 'Screen Recorder'
    $shortcut.Save()
    Say "已创建开始菜单快捷方式"
} catch {
    Say "警告：快捷方式创建失败（$($_.Exception.Message)）——不影响使用"
}

# 卸载登记（HKCU，免管理员）
try {
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\ScreenRecorder'
    New-Item -Path $key -Force | Out-Null
    Set-ItemProperty -Path $key -Name 'DisplayName' -Value 'Screen Recorder'
    Set-ItemProperty -Path $key -Name 'DisplayVersion' -Value '0.1.0'
    Set-ItemProperty -Path $key -Name 'Publisher' -Value 'Screen Recorder'
    Set-ItemProperty -Path $key -Name 'InstallLocation' -Value $InstallDir
    Set-ItemProperty -Path $key -Name 'UninstallString' -Value "pwsh -File `"$(Join-Path $InstallDir 'uninstall.ps1')`""
    Set-ItemProperty -Path $key -Name 'NoModify' -Value 1
    Set-ItemProperty -Path $key -Name 'NoRepair' -Value 1
    Say "已登记卸载项"
} catch {
    Say "警告：卸载登记失败（$($_.Exception.Message)）"
}

# 把 uninstall.ps1 复制到安装目录（卸载登记指向它）
Copy-Item (Join-Path $root 'uninstall.ps1') (Join-Path $InstallDir 'uninstall.ps1') -Force
Say "安装完成。启动：$target\sr-ui.exe"
exit 0
