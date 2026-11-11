# SELFTEST: S1-011

## 0. 开工四项自查
| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01:56` |
| 必读章节可定位 | 通过 | `CONTRACT §2`（S1 域 `src/recorder/`）、轮43指令 §1 P1 |
| 上游交付物可获取 | 通过 | `CaptureRequest.h`、`WgcCaptureSource.cpp` |
| 文件域无交集 | 通过 | 本卡仅 `src/recorder/`；`src/capture/` 未动 |

## 1. 自测命令与输出
| 命令 | 结果 |
| --- | --- |
| 三构建编译 | exit=0 warnings=0 errors=0 |
| `sr-recorder.exe --record --seconds 3 --cursor off --out <f>` | `record_exit=0 include_cursor=0` |
| 同上 `--cursor on` | `record_exit=0 include_cursor=1` |
| `pwsh -File verify/cases/g5_r43_closeout.ps1` | M1-24 六条断言全绿 |
| `sr-recorder.exe --cursor bogus` | 解析失败（不静默回落） |

## 2. 红-绿对照
| 断言 | 旧值 | 新值 |
| --- | --- | --- |
| `--cursor off` 成片指针像素 | 无入口，恒含光标 | 0（中心 + 全画面抽样） |
| `--cursor on` 指针随位置 | 恒含但不可控 | A/B 两点各 35，旧点 0 |

## 3. 回归
全量回归见主卡 `artifacts/r43-regression.txt`。

## 4. DoD 勾选
- [x] 开工四项自查
- [x] 一条需求一条证据（含命令与实测值）
- [x] 先红后绿、三构建零告警
- [x] 未改接口签名、未引依赖、未越域、未改 `verify/`
- [x] 已知限制写明；四件套齐全；未自评为已验收