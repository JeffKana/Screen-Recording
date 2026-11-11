# 轮 42 · M8-05#1 长窗口护栏的纯计算函数（verify/lib，供用例与自检共用）
#
# 裁定 A + 长窗口护栏（验收报告-轮41 §2、轮42指令 §2）：
#   主判据用配置码率，几乎等于产品公式（只能防读错 free / 漏减 stop）；
#   护栏用「>=60s 真实录制的文件字节数 / 时长」得到平均写入速率，
#   与配置码率比偏差 <=20%。文件字节数来自文件系统、时长来自墙钟/产品 wall_seconds，
#   **不消费产品的 estimated_record_seconds**，所以不是产品公式的复读机。
#
# 配置码率口径：--bitrate 6000 kbps + 音频 192 kbps = 6192 kbps
#   -> 6192 * 1000 / 8 = 774000 B/s
#   192 的出处：src/core/util/DiskEstimate.h:8 `kAudioBitrateKbps = 192`
#               src/recorder/RecordingSession.cpp:275 `options.bitrate_kbps + 192`

$script:SrAudioBitrateKbps = 192

function Get-ConfiguredBytesPerSec {
    param([double]$VideoBitrateKbps = 6000)
    return ($VideoBitrateKbps + $script:SrAudioBitrateKbps) * 1000.0 / 8.0
}

function Get-LongWindowDeviation {
    param(
        [double]$FileBytes,
        [double]$WallSeconds,
        [double]$ConfiguredBps,
        [double]$MinWallSeconds = 60.0,
        [double]$MaxDeviation = 0.20
    )
    # 数据缺失 / 录制不足 60s 一律判 FAIL（Invalid），拒绝 SKIP：
    # 验收方明确拒 C「新增 SKIP 前置」，SKIP 会把真实超标藏起来（D-58 同族）。
    if ($FileBytes -le 0) {
        return [pscustomobject]@{ Valid = $false; Reason = 'file_bytes<=0（成片缺失或 0 字节）';
                                  AvgBps = 0.0; Deviation = 1.0; Pass = $false }
    }
    if ($WallSeconds -lt $MinWallSeconds) {
        return [pscustomobject]@{ Valid = $false;
                                  Reason = ("wall_seconds={0:N2} < {1}s（短窗口不能当长窗口基准）" -f $WallSeconds, $MinWallSeconds);
                                  AvgBps = 0.0; Deviation = 1.0; Pass = $false }
    }
    if ($ConfiguredBps -le 0) {
        return [pscustomobject]@{ Valid = $false; Reason = 'configured_bps<=0（配置码率不可用）';
                                  AvgBps = 0.0; Deviation = 1.0; Pass = $false }
    }
    $avg = $FileBytes / $WallSeconds
    $dev = [Math]::Abs($avg - $ConfiguredBps) / $ConfiguredBps
    return [pscustomobject]@{ Valid = $true; Reason = ''; AvgBps = $avg; Deviation = $dev;
                              Pass = ($dev -le $MaxDeviation) }
}
