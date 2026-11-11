# HANDOFF: S5-004（存储守护收口）

## 1. 我改了什么接口？

**没有新增或修改任何跨模块接口。** 改动只有三处：

| 文件 | 改动 | 性质 |
| --- | --- | --- |
| `src/app/AppConfig.h` | 新增 `disk_warn_mb`（默认 2048）、`disk_critical_mb`（默认 500） | 新增两个**配置项**（`[output]` 段） |
| `src/app/AppConfig.cpp` | 读取上述两项（0 视为配置损坏并报错） | 配置解析 |
| `src/app/main.cpp` | `RefreshDiskRemaining` 增加水位判定与两行日志；`UpdateUi` 把提示放到状态栏第一行最前 | 纯 UI 呈现 |

`src/recorder/`、`src/core/`、`src/ipc/` **未改** → **不需要契约变更单**，IPC 协议版本不变。

## 2. 下游需要接什么？

| 下游 | 要做什么 |
| --- | --- |
| 验收（V1） | 重跑 `verify/cases/g5_storage.ps1`（13 PASS / 0 FAIL）；覆盖表四条子句应转为「已实现」 |
| 后续轮次 | 若要把提示线做进设置页（UI 的 `[output]` 段目前只能在 INI 里改），另开 S5 卡；本卡没做设置页入口 |

## 3. 已知限制是什么？

1. **磁盘水位是「目标卷」的可用量**，不是「输出文件所在目录的配额」；
   `StorageGuard::Refresh` 向上找第一个已存在的父目录来问 `GetDiskFreeSpaceExW`。
2. **强提示线是 UI 侧判的**（`src/app/`）。recorder 侧的 `StorageGuard::Status::critical`
   仍然没有消费者——本卡没动 `src/recorder/`（S1 文件域）。
   后果：**直接用 `sr-recorder --record`（不经 UI）时看不到强提示**，
   只有非阻塞 warn 日志与停止线行为。这是有意为之（跨域），但必须写明。
3. 提示线可配置（`[output] disk_warn_mb` / `disk_critical_mb`）是为了让验收能触发/不触发，
   默认值仍是需求口径（2GB / 500MB）。部署方若改这两个值，**责任在配置**。

## 4. 哪些需求我**没**做完？

| 需求 | 状态 | 说明 |
| --- | --- | --- |
| M8-03#1 / #2 / M8-04#1 / M8-05#1 | 已实现 | 四条都有断言 + 实测值，见 `EVIDENCE.md` |

**本卡范围内没有未做完的子句。** 但要如实说明两点：

1. **M8-03#2 的「强提示」只在 UI 侧生效**（见 §3 第 2 条）——recorder 直连路径没有强提示。
2. **M8-03#2 之前确实未实现**（覆盖表写「已实现」是错的）：`StorageGuard::Status::critical`
   无消费者、阈值也不是 500MB。本卡补上的是 UI 侧的呈现与判定。
