# G4 门 60s 素材的「运动源」：一个置顶黑底 + 水平往返红块。
# 用途：让采集侧有真实变化，重复帧才有产品级意义（静止桌面本来就会重复）。
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$form = New-Object System.Windows.Forms.Form
$form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::None
$form.TopMost = $true
$form.StartPosition = 'Manual'
$form.Location = New-Object System.Drawing.Point(80, 80)
$form.Size = New-Object System.Drawing.Size(480, 320)
$form.BackColor = [System.Drawing.Color]::Black
$form.ShowInTaskbar = $false

$box = New-Object System.Windows.Forms.Panel
$box.Size = New-Object System.Drawing.Size(140, 140)
$box.BackColor = [System.Drawing.Color]::Red
$form.Controls.Add($box)

$state = @{ x = 0; dir = 1 }
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 16
$timer.Add_Tick({
    $state.x += 11 * $state.dir
    if ($state.x -gt 320) { $state.dir = -1 }
    if ($state.x -lt 0) { $state.dir = 1 }
    $box.Left = $state.x
    $box.Top = 80 + [int](40 * [Math]::Sin($state.x / 20.0))
})
$timer.Start()
[System.Windows.Forms.Application]::Run($form)
