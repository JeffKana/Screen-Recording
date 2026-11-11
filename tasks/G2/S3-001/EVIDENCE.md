# EVIDENCE: S3-001 音频链路

统一命令：

```powershell
pwsh -NoProfile -File verify/cases/g2_walk.ps1 -BuildDir build-mingw
```

## M1-15 系统声音采集

```
loopback_rate=44100   loopback_channels=2
loopback_peak_max=0.0000   （测试时段桌面完全静音，属预期）
```

- 结论：**PASS（受限）**。回环端点初始化成功、格式协商正确（44.1kHz/2ch 为共享模式混音格式）。
  测试机上无音频播放，因此只验证了「静音时仍产出合法静音轨」，未验证真实声音内容。

## M1-17 麦克风采集与电平

```
mic_used=1   mic_peak_max=0.5645   mic_produced_frames=264203
```

- 结论：**PASS**。麦克风端点可用，全程峰值 0.56（真实环境噪声/启动瞬态），说明实际采到了信号。

## M1-18 / M1-20 混音与独立增益

- 方式：`srprobe mixselftest`，用合成信号（系统声 0.35、麦克风 0.25）验证混音规则。
- 输出：

```
PASS mix_sum      actual=0.6000 expected=0.6000
PASS no_clip      actual=0.0000 expected=0.0000
PASS clip_to_one  actual=1.0000 expected=1.0000
PASS mute_mic     actual=0.3500 expected=0.3500
PASS mute_system  actual=0.2500 expected=0.2500
failures=0
```

- 结论：**PASS**。两路叠加正确、2 倍增益触发限幅并置 `LastBlockClipped`、任一路静音不影响另一路。

## M1-21 静音期时间轴连续性（本卡核心）

- 方式：`srprobe audio --seconds 6`，系统静音、麦克风有声。断言 `expected_frames == produced_frames`。
- 多轮输出：

```
expected_frames=264467  produced_frames=264467  duration_drift_frames=0
expected_frames=263982  produced_frames=263982  duration_drift_frames=0
expected_frames=132079  produced_frames=132079  duration_drift_frames=0   (--no-mic)
silence_filled=264467  source_gaps=397
```

- 结论：**PASS**。时间轴由 QPC 驱动而非包驱动，输出帧数与时长严格相等，漂移恒为 0；
  `source_gaps` 约等于块数（每 20ms 一个缺包），全部被静音填充吸收，无累积误差。

## M8-11 默认音频设备变更

- 结论：**BLOCKED**。需要物理插拔/切换默认输出设备，测试环境无可用操作手段。
  代码已实现「每约 1 秒比对默认端点 id，变化即置 `NeedsRebuild`」，但未做实机验证。

## 汇总

| 需求 | 结论 | 说明 |
| --- | --- | --- |
| M1-15 | PASS（受限） | 端点与格式正确；无播放内容 |
| M1-17 | PASS | 峰值 0.5645，采到真实信号 |
| M1-18 | PASS | 混音叠加 0.35+0.25=0.60 |
| M1-20 | PASS | 增益/静音/限幅 5 项断言全绿 |
| M1-21 | PASS | 三轮 drift 均为 0 |
| M8-11 | BLOCKED | 缺设备切换手段 |
