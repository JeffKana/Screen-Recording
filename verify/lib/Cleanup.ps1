# verify/lib/Cleanup.ps1 —— 验收脚本的统一异常清理（轮 30 / F2）
#
# 背景：上一轮的孤儿进程只修在 g4_gate 一个文件里，其余 13 个脚本都没有 trap。
# 这里把「登记 spawn 进程 + trap 统一收尾」抽成一份，所有脚本 dot-source 即可：
#
#   . (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
#   $p = Start-Process -FilePath $ui -PassThru
#   Register-SrProcess $p
#
# 中途抛异常时由各脚本自己的 trap 调 Clear-SrProcesses；trap 必须写在调用方作用域
# （dot-source 进来的 trap 不接管调用方的终止错误，已实测），所以脚本里还要有这一行：
#
#   trap { Clear-SrProcesses -IncludeNameFallback; Write-Host "FAIL: $_"; exit 1 }
#
# 正常路径请显式 Stop-SrProcess / WaitForExit，别依赖 trap。

$script:SrCleanupRegistered = New-Object System.Collections.ArrayList

function Register-SrProcess {
    param($Process)
    if ($null -eq $Process) { return }
    if (-not $script:SrCleanupRegistered.Contains($Process)) {
        [void]$script:SrCleanupRegistered.Add($Process)
    }
}

function Unregister-SrProcess {
    param($Process)
    if ($null -eq $Process) { return }
    [void]$script:SrCleanupRegistered.Remove($Process)
}

function Stop-SrProcess {
    param($Process)
    try {
        if ($null -ne $Process -and -not $Process.HasExited) {
            Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
            [void]$Process.WaitForExit(3000)
        }
    } catch {
        # 清理路径本身不允许再抛：否则 trap 里二次异常会掩盖原始失败。
    }
}

function Clear-SrProcesses {
    param([switch]$IncludeNameFallback)
    foreach ($process in @($script:SrCleanupRegistered)) {
        Stop-SrProcess $process
    }
    $script:SrCleanupRegistered.Clear()
    if ($IncludeNameFallback) {
        Get-Process sr-ui, sr-recorder -ErrorAction SilentlyContinue |
            Stop-Process -Force -ErrorAction SilentlyContinue
    }
}

# 读日志/INI 的安全版本：
#   - 用 FileShare.ReadWrite 打开（被测进程仍持有写句柄时 ReadAllText 会抛共享冲突）；
#   - 任何异常都返回空串，不再出现「读日志失败 → 整个用例中断」。
function Read-TextSafe {
    param([string]$Path)
    try {
        if (-not (Test-Path $Path)) { return '' }
        $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
                                          [System.IO.FileAccess]::Read,
                                          [System.IO.FileShare]::ReadWrite)
        $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
        $text = $reader.ReadToEnd()
        $reader.Dispose()
        $stream.Dispose()
        return $text
    } catch {
        return ''
    }
}

