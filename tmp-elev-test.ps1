# 临时实验 v2：确认本机能否无交互构造高完整性（High IL）进程 + 高完整性窗口。
$ErrorActionPreference = 'Stop'
$out = Join-Path $env:TEMP 'sr-elev-probe.txt'
$inner = Join-Path $env:TEMP 'sr-elev-inner.ps1'
Remove-Item $out -Force -ErrorAction SilentlyContinue

@'
$id = [Security.Principal.WindowsIdentity]::GetCurrent()
$p = New-Object Security.Principal.WindowsPrincipal($id)
$elev = $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$out = Join-Path $env:TEMP 'sr-elev-probe.txt'
("Elevated=" + $elev + " Name=" + $id.Name) | Out-File -FilePath $out -Encoding utf8
'@ | Set-Content -Path $inner -Encoding UTF8

$p = Start-Process -FilePath 'pwsh.exe' -Verb RunAs -ArgumentList @('-NoProfile', '-File', $inner) -PassThru -WindowStyle Hidden
Write-Host ("launched pid=" + $p.Id)

$deadline = (Get-Date).AddSeconds(20)
while ((Get-Date) -lt $deadline) {
    if (Test-Path $out) { break }
    Start-Sleep -Milliseconds 300
}
if (Test-Path $out) {
    Write-Host 'PROBE_OUTPUT:'
    Get-Content $out
} else {
    Write-Host 'PROBE_NO_OUTPUT'
    Write-Host ("alive=" + (-not $p.HasExited))
    Get-Process -Name consent, CredentialUIBroker, pwsh -ErrorAction SilentlyContinue |
        Select-Object ProcessName, Id, StartTime | Format-Table -AutoSize | Out-String | Write-Host
}
