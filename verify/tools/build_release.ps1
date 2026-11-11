#!/usr/bin/env pwsh
# 轮 40 · G6 重打包：发布构建 + 产物白名单自检 + 打包 + **包内 exe 指纹比对**
#
# 依据 docs/11b-轮36-G6交付方案.md：
#   * 发布构建开关：-DSR_TEST_HOOKS=OFF（去掉验收注入开关）+ -DSR_BUILD_TOOLS=OFF（不编译调试工具）
#   * 产物白名单：只允许 sr-ui.exe / sr-recorder.exe（+ 不入包的 pdb/obj）
#   * 包结构：bin/ + install.ps1 + uninstall.ps1 + README.txt + VERSION.txt
#   * 不打包 .pdb / verify/ / docs/
#
# 轮 40 新增（验收报告 F5）：
#   * 打包脚本**自带**「包内 exe SHA256 == build-release 产物 SHA256」的比对，
#     不靠人肉核对；比对对象是**从 zip 解压出来的文件**，不是暂存目录。
#   * 记录源指纹（src/ + CMakeLists.txt + vcpkg.json 的 SHA256 汇总）与两个 exe 的 SHA256，
#     写进 dist/release-summary.json，便于核对「包出自哪一份源码」。
#   * -SkipBuild 不再是「无声通过」：会记 built_now=false；可用
#     -ExpectedSourceFingerprint 断言「当前源码没变过」，源码变了直接 FAIL。
#
# 用法：pwsh -File verify/tools/build_release.ps1 [-Version 0.1.0] [-SkipBuild]
#                            [-DevBuildDir build-msvc] [-ExpectedSourceFingerprint <sha256>]

param(
    [string]$Version = '0.1.0',
    [string]$BuildDir = 'build-release',
    [string]$DistDir = 'dist',
    [switch]$SkipBuild,
    [string]$DevBuildDir = 'build-msvc',
    [string]$ExpectedSourceFingerprint = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location $root
try {
    $script:pass = 0
    $script:fail = 0
    function Assert {
        param([string]$Name, [bool]$Ok, [string]$Detail)
        if ($Ok) { $script:pass++; Write-Host ("  PASS {0} — {1}" -f $Name, $Detail) }
        else { $script:fail++; Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail) }
    }

    Write-Host '--- 1 发布构建（SR_TEST_HOOKS=OFF / SR_BUILD_TOOLS=OFF）---'
    $builtNow = -not $SkipBuild
    if (-not $SkipBuild) {
        & cmake -S . -B $BuildDir -G 'Visual Studio 17 2022' -A x64 `
            -DSR_TEST_HOOKS=OFF -DSR_BUILD_TOOLS=OFF | Out-Null
        if ($LASTEXITCODE -ne 0) { Write-Host 'FAIL: cmake configure'; exit 1 }
        & cmake --build $BuildDir --config Release | Out-Null
        if ($LASTEXITCODE -ne 0) { Write-Host 'FAIL: cmake build'; exit 1 }
    } else {
        Write-Host '  [警告] -SkipBuild：本次没有重新编译，产物可能是陈旧的（built_now=false）'
    }
    $binDir = Join-Path $root (Join-Path $BuildDir 'bin\Release')
    if (-not (Test-Path $binDir)) {
        $binDir = Join-Path $root (Join-Path $BuildDir 'bin')
    }
    Assert 'release-bin-exists' (Test-Path (Join-Path $binDir 'sr-ui.exe')) "产物目录 $binDir"

    Write-Host '--- 1b 产物指纹（SHA256）与源指纹 ---'
    function Get-Sha256 {
        param([string]$Path)
        if (-not (Test-Path $Path)) { return '' }
        return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToUpperInvariant()
    }
    function Get-SourceFingerprint {
        # 对全部产品源码 + 构建脚本取指纹：两个构建目录只要出自同一份源码树，指纹就相同。
        $files = @()
        $files += Get-ChildItem -Path (Join-Path $root 'src') -Recurse -File |
            Where-Object { $_.Extension -in @('.cpp', '.h', '.txt') }
        $files += Get-Item (Join-Path $root 'CMakeLists.txt')
        $files += Get-Item (Join-Path $root 'vcpkg.json')
        $lines = @()
        foreach ($f in ($files | Sort-Object FullName)) {
            $rel = $f.FullName.Substring($root.Length).Replace('\', '/')
            $lines += ($rel + ':' + (Get-Sha256 $f.FullName))
        }
        $bytes = [System.Text.Encoding]::UTF8.GetBytes(($lines -join "`n"))
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '') }
        finally { $sha.Dispose() }
    }
    $sourceFingerprint = Get-SourceFingerprint
    $uiSha = Get-Sha256 (Join-Path $binDir 'sr-ui.exe')
    $recSha = Get-Sha256 (Join-Path $binDir 'sr-recorder.exe')
    Write-Host ("  sr-ui.exe       SHA256 = {0}" -f $uiSha)
    Write-Host ("  sr-recorder.exe SHA256 = {0}" -f $recSha)
    Write-Host ("  source_fingerprint        = {0}" -f $sourceFingerprint)
    Assert 'release-exe-sha-present' (($uiSha -ne '') -and ($recSha -ne '')) '两个 exe 都能算出 SHA256'
    if ($ExpectedSourceFingerprint -ne '') {
        Assert 'source-fingerprint-unchanged' ($sourceFingerprint -eq $ExpectedSourceFingerprint.ToUpperInvariant()) `
            ("当前源码指纹 {0}（期望 {1}）→ 用 -SkipBuild 打出来的包可能来自旧源码" -f `
             $sourceFingerprint, $ExpectedSourceFingerprint.ToUpperInvariant())
    }
    # 开发构建（SR_TEST_HOOKS=ON）的 exe 指纹：用于核对「同源」。
    $devBinDir = Join-Path $root (Join-Path $DevBuildDir 'bin\Release')
    if (-not (Test-Path $devBinDir)) { $devBinDir = Join-Path $root (Join-Path $DevBuildDir 'bin') }
    $devUiSha = Get-Sha256 (Join-Path $devBinDir 'sr-ui.exe')
    $devRecSha = Get-Sha256 (Join-Path $devBinDir 'sr-recorder.exe')
    Write-Host ("  [开发构建 {0}] sr-ui={1} sr-recorder={2}" -f $DevBuildDir, $devUiSha, $devRecSha)
    # 说明：发布构建按 G6 方案关掉了 SR_TEST_HOOKS/SR_BUILD_TOOLS，与开发构建**必然**不同哈希；
    # 「同源」由 source_fingerprint 证明，「同 SHA」在不关测试钩子的前提下不可满足。
    $devSameSha = ($devUiSha -eq $uiSha) -and ($devRecSha -eq $recSha)
    Write-Host ("  dev/release 同 SHA = {0}（发布构建按方案关闭 SR_TEST_HOOKS，哈希必然不同；同源看 source_fingerprint）" -f $devSameSha)

    Write-Host '--- 2 产物白名单自检 ---'
    $files = @(Get-ChildItem $binDir -File -ErrorAction SilentlyContinue)
    $exes = @($files | Where-Object { $_.Extension -eq '.exe' } | ForEach-Object { $_.Name })
    Assert 'release-only-product-exe' (
        ($exes | Where-Object { $_ -notin @('sr-ui.exe', 'sr-recorder.exe') }).Count -eq 0) `
        ("产物 exe：{0}（期望只有 sr-ui.exe / sr-recorder.exe）" -f ($exes -join ','))
    Assert 'release-no-debug-tools' (
        (@('srctl.exe', 'srprobe.exe') | Where-Object { $exes -contains $_ }).Count -eq 0) `
        ("调试工具存在性检查：srctl/srprobe 都不在包里（当前：{0}）" -f ($exes -join ','))

    $help = (& (Join-Path $binDir 'sr-recorder.exe') --help 2>&1 | Out-String)
    $debugSwitches = @('--inject-drop-every', '--fail-write-after', '--fail-encoder',
                        '--preflight-fail', '--fallback-dir', '--diag-window-test')
    $leaked = @($debugSwitches | Where-Object { $help -match [regex]::Escape($_) })
    Assert 'release-help-no-debug-switch' ($leaked.Count -eq 0) `
        ("--help 里残留的调试开关：{0}（期望 0）" -f ($(if ($leaked.Count -gt 0) { $leaked -join ',' } else { '无' })))
    # 轮 41 R4：发布包 --help 必须列出四个产品参数（此前功能在、help 没列 → 用户不可发现）。
    $requiredHelpParams = @('--record', '--window', '--fit', '--region')
    $missingHelp = @($requiredHelpParams | Where-Object { $help -notmatch [regex]::Escape($_) })
    Assert 'release-help-four-params' ($missingHelp.Count -eq 0) `
        ("--help 缺少的参数：{0}（期望 0）" -f ($(if ($missingHelp.Count -gt 0) { $missingHelp -join ',' } else { '无' })))
    $recorderVersion = (& (Join-Path $binDir 'sr-recorder.exe') --version 2>&1 | Out-String).Trim()
    Assert 'release-version-ok' ($recorderVersion -match 'sr-recorder' -and $recorderVersion -match 'test_hooks=0') `
        ("--version 输出：{0}" -f $recorderVersion)

    Write-Host '--- 3 打包 ---'
    $stage = Join-Path $env:TEMP ("sr-release-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    $packageRoot = Join-Path $stage "ScreenRecorder-$Version-win-x64"
    New-Item -ItemType Directory -Force -Path (Join-Path $packageRoot 'bin') | Out-Null
    Copy-Item (Join-Path $binDir 'sr-ui.exe') (Join-Path $packageRoot 'bin') -Force
    Copy-Item (Join-Path $binDir 'sr-recorder.exe') (Join-Path $packageRoot 'bin') -Force
    Copy-Item (Join-Path $root 'packaging\install.ps1') $packageRoot -Force
    Copy-Item (Join-Path $root 'packaging\uninstall.ps1') $packageRoot -Force
    Copy-Item (Join-Path $root 'packaging\README.txt') $packageRoot -Force
    $gitSha = 'no-git'
    if (Get-Command git -ErrorAction SilentlyContinue) {
        $sha = (& git -C $root rev-parse --short HEAD 2>$null | Out-String).Trim()
        if ($sha) { $gitSha = $sha }
    }
    $versionText = @(
        "name=ScreenRecorder",
        "version=$Version",
        "build_sha=$gitSha",
        "build_time=$([DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))",
        "build_flags=SR_TEST_HOOKS=OFF;SR_BUILD_TOOLS=OFF"
    ) -join "`n"
    [System.IO.File]::WriteAllText((Join-Path $packageRoot 'VERSION.txt'), $versionText + "`n",
                                   [System.Text.UTF8Encoding]::new($false))
    $zipPath = Join-Path $root (Join-Path $DistDir "ScreenRecorder-$Version-win-x64.zip")
    New-Item -ItemType Directory -Force -Path (Join-Path $root $DistDir) | Out-Null
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Compress-Archive -Path (Join-Path $stage 'ScreenRecorder-*') -DestinationPath $zipPath -Force
    $zipItems = @([System.IO.Compression.ZipFile]::OpenRead($zipPath)).Entries | ForEach-Object { $_.FullName }
    Assert 'zip-no-pdb' (@($zipItems | Where-Object { $_ -match '\.pdb$' }).Count -eq 0) `
        ("包内 pdb 数：{0}（期望 0）" -f @($zipItems | Where-Object { $_ -match '\.pdb$' }).Count)
    Assert 'zip-no-verify-docs' (
        (@($zipItems | Where-Object { $_ -match '(^|/)(verify|docs)/' }).Count -eq 0)) `
        ("包内 verify/docs 条目数：{0}（期望 0）" -f @($zipItems | Where-Object { $_ -match '(^|/)(verify|docs)/' }).Count)
    $zipLeaves = @($zipItems | ForEach-Object { Split-Path $_ -Leaf })
    $missing = @(@('install.ps1', 'uninstall.ps1', 'README.txt', 'VERSION.txt') |
        Where-Object { $zipLeaves -notcontains $_ })
    Assert 'zip-has-install-scripts' ($missing.Count -eq 0) `
        ("包内条目：{0}；缺：{1}" -f ($zipItems -join ', '), ($(if ($missing.Count -gt 0) { $missing -join ',' } else { '无' })))

    Write-Host '--- 4 解压即用验证 ---'
    $extract = Join-Path $env:TEMP ("sr-extract-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    Expand-Archive -Path $zipPath -DestinationPath $extract -Force
    $extracted = Get-ChildItem $extract -Directory | Select-Object -First 1
    $extractedRec = Join-Path $extracted.FullName 'bin\sr-recorder.exe'
    $extractedUi = Join-Path $extracted.FullName 'bin\sr-ui.exe'
    Assert 'extract-runnable' ((Test-Path $extractedRec) -and (Test-Path $extractedUi)) `
        ("解压后：{0}" -f $extracted.FullName)
    if (Test-Path $extractedRec) {
        $extractedVersion = (& $extractedRec --version 2>&1 | Out-String).Trim()
        Assert 'extract-version-ok' ($extractedVersion -match 'test_hooks=0') `
            ("解压后 --version：{0}" -f $extractedVersion)
        $extractedHelp = (& $extractedRec --help 2>&1 | Out-String)
        Assert 'extract-help-no-debug' (
            (@($debugSwitches | Where-Object { $extractedHelp -match [regex]::Escape($_) }).Count -eq 0)) `
            '解压后 --help 无调试开关'
        # 轮 41 R4：判据是「包内 exe 的 help」含四个串，所以必须在解压产物上再验一次。
        $missingExtractedHelp = @($requiredHelpParams | Where-Object { $extractedHelp -notmatch [regex]::Escape($_) })
        Assert 'extract-help-four-params' ($missingExtractedHelp.Count -eq 0) `
            ("解压后 --help 缺少的参数：{0}（期望 0）" -f ($(if ($missingExtractedHelp.Count -gt 0) { $missingExtractedHelp -join ',' } else { '无' })))
        # sr-ui 是 GUI 子系统程序：stdout 不接控制台，--version 的文字看不到，
        # 但进程能正常启动并以 0 退出——这正是「解压即用」要验的。
        & $extractedUi --version | Out-Null
        Assert 'extract-ui-runs' ($LASTEXITCODE -eq 0) ("解压后 sr-ui --version 退出码={0}（期望 0）" -f $LASTEXITCODE)
    }

    Write-Host '--- 4b 包内 exe 指纹比对（包内 == build-release 产物）---'
    $zipUiSha = Get-Sha256 $extractedUi
    $zipRecSha = Get-Sha256 $extractedRec
    Assert 'zip-ui-sha-matches-build' (($zipUiSha -ne '') -and ($zipUiSha -eq $uiSha)) `
        ("包内 sr-ui SHA256={0}；构建目录={1}" -f $zipUiSha, $uiSha)
    Assert 'zip-recorder-sha-matches-build' (($zipRecSha -ne '') -and ($zipRecSha -eq $recSha)) `
        ("包内 sr-recorder SHA256={0}；构建目录={1}" -f $zipRecSha, $recSha)


    $summaryPath = Join-Path $root (Join-Path $DistDir 'release-summary.json')
    # 与上一版摘要比对：指纹变了才说明「重新打过包」。
    $previousUiSha = ''
    if (Test-Path $summaryPath) {
        try {
            $previous = Get-Content $summaryPath -Raw | ConvertFrom-Json
            if ($previous.PSObject.Properties.Name -contains 'sr_ui_sha256') { $previousUiSha = $previous.sr_ui_sha256 }
        } catch { $previousUiSha = '' }
    }
    $summary = [pscustomobject]@{
        version = $Version
        zip = $zipPath
        zip_bytes = (Get-Item $zipPath).Length
        build_dir = $BuildDir
        flags = 'SR_TEST_HOOKS=OFF;SR_BUILD_TOOLS=OFF'
        built_now = $builtNow
        source_fingerprint = $sourceFingerprint
        sr_ui_sha256 = $uiSha
        sr_recorder_sha256 = $recSha
        zip_sr_ui_sha256 = $zipUiSha
        zip_sr_recorder_sha256 = $zipRecSha
        zip_sha_matches_build = (($zipUiSha -eq $uiSha) -and ($zipRecSha -eq $recSha))
        dev_build_dir = $DevBuildDir
        dev_sr_ui_sha256 = $devUiSha
        dev_sr_recorder_sha256 = $devRecSha
        dev_same_sha_as_release = $devSameSha
        previous_sr_ui_sha256 = $previousUiSha
        exe_sha_changed_vs_previous = ($previousUiSha -eq '' -or $previousUiSha -ne $uiSha)
        pass = $script:pass
        fail = $script:fail
    }
    $summary | ConvertTo-Json -Depth 3 | Set-Content -Path $summaryPath -Encoding UTF8
    Write-Host ''
    Write-Host ("RESULT: {0} PASS / {1} FAIL ; 包 {2}（{3:N0} 字节）；摘要 {4}" -f `
        $script:pass, $script:fail, $zipPath, (Get-Item $zipPath).Length, $summaryPath)
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item $extract -Recurse -Force -ErrorAction SilentlyContinue
    if ($script:fail -gt 0) { exit 1 }
    exit 0
} finally {
    Pop-Location
}
