# EVIDENCE: S2-001 采集链路

复现环境：Windows 11 10.0.26200 / MinGW-w64 GCC 16.2 / Ninja / 双显示器 1920×1080。

统一命令：

```powershell
pwsh -NoProfile -File verify/cases/g2_walk.ps1 -BuildDir build-mingw
```

## M1-01 全屏录制

- 方式：`srprobe capture --source display --seconds 12 --stimulate`（stimulate 移动一个置顶小窗制造桌面变化）
- 会话**活跃**时实测（两轮）：

```
backend=dxgi-dd   size=1920x1080   seconds=12.03   frames=719/721
fps_actual=59.82 / 59.96   interval_avg_ms=16.25   interval_p99_ms=31
interval_max_ms=32   frames_over_2x_avg=0   rebuilds=0
```

- 会话**空闲**（锁屏/休眠）时复测：`interval_avg_ms=787`、`frames=15`、`dwm_throttled=1`。
- 结论：**PARTIAL**。活跃会话下指标达标（≥55fps、零抖动、零重建），但当前环境已无法复现，
  按 E6 记为挂账。分辨率一致性始终通过（输出纹理 == 显示器分辨率 1920×1080）。

## M1-02 多显示器枚举

```
outputs=2
[0] \\.\DISPLAY2 1920x1080 at (0,0)
[1] \\.\DISPLAY1 1920x1080 at (1920,0)
```

- 结论：**PASS**。序号、分辨率、虚拟桌面坐标与系统显示设置一致。

## M1-04 / M1-05 区域录制与尺寸修正

```
requested 800x600  -> size=800x600   (PASS 精确匹配)
requested 801x601  -> size=800x600   (PASS 偶数对齐修正)
```

- 结论：**PASS**。裁剪由 `CopySubresourceRegion` 完成，未引入缩放，因此选区与输出像素一一对应。

## M1-07 单窗口录制

- 方式：`srprobe windowselftest` 自建 GDI 窗口 → PrintWindow 采集 → 回读像素统计。
- 输出：

```
window_size=406x312
non_black_ratio=0.9824
green_ratio=0.8783
```

- 结论：**PASS**。画面非全黑且 87.8% 像素为窗口绘制的绿色，说明拿到的是窗口自身内容而不是屏幕像素。

## M6-06 混合 DPI

- 结论：**BLOCKED**。参考机双屏均为 100% 缩放，无 150%/200% 环境，无法验证跨屏坐标换算。
  代码层已按物理像素请求、输出尺寸与请求一致（M1-04 间接佐证），但不构成 M6-06 的通过判据。

## 汇总

| 需求 | 结论 | 说明 |
| --- | --- | --- |
| M1-01 | PARTIAL | 活跃会话实测达标，当前环境不可复现（E6 挂账） |
| M1-02 | PASS | 2 显示器枚举正确 |
| M1-04 | PASS | 选区尺寸精确 |
| M1-05 | PASS | 奇数尺寸修正为偶数 |
| M1-07 | PASS | 窗口内容非黑，87.8% 命中绘制色 |
| M6-06 | BLOCKED | 缺高 DPI 显示器 |
