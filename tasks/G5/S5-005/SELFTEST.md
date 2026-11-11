# SELFTEST: S5-005

## 0. 开工四项自查
| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01:112` M3-10、`docs/01:206` M6-11，逐条核过验收要点 |
| 必读章节可定位 | 通过 | `CONTRACT §2`、`docs/06 §4.2`、`verify/cases/ui_smoke.ps1` 103 条 |
| 上游交付物可获取 | 通过 | 轮 40 S5-003、轮 41 S1-009 交付；`AppConfig`、`SettingsProc` 现状普查 |
| 文件域无交集 | 通过 | 本卡 `src/app/`；S1-010 `src/recorder/`、S2-003 `src/compositor/`、V1 卡 `verify/`+`docs/` |

## 1. 自测命令与输出
| 命令 | 结果 |
| --- | --- |
| `cmake --build build-msvc --config Release/Debug` / `build-mingw` | exit=0 warnings=0 |
| `pwsh -File verify/cases/g5_r42_closeout.ps1` | 12/12（其中本卡 8 条） |
| `pwsh -File verify/cases/ui_smoke.ps1` ×3 | 见主卡 `artifacts/r42-regression.txt` |
| `pwsh -File verify/cases/g5_closeout.ps1` | 18 PASS（M6-11#1 正则未被破坏） |
| `pwsh -File verify/tools/build_release.ps1` | 17/17 |

## 2. 红-绿对照
| 断言 | 旧值 | 新值 |
| --- | --- | --- |
| 画质档位 | 设置页无档位概念 | 四档联动 + 界面可见 + 高级覆盖 |
| 背压标识 | 丢帧时状态栏无任何标识 | 「⚠ 背压（丢帧 N）」，正常态不误报 |
| 档位名显示 | 首轮乱码「鏋佽嚧」、下拉序号 4 | 「极致」正确、序号 0 |
| 编译 | 首轮 `RefreshPresetValues(*context)` 对引用解引用报 C2100 | 传引用 → 三构建零告警 |

## 3. 回归
见主卡 `artifacts/r42-regression.txt`；`ui_smoke` 3×103、`g5_closeout` 18 为关键回归。

## 4. DoD 勾选
- [x] 开工四项自查通过
- [x] 每条需求 → 一条证据，含命令与实测值
- [x] 首轮编译错误与 INI 编码缺陷已修，判据未放宽
- [x] 新增控件 ID 108/109 不占用既有 ID；正常态状态栏文本不变
- [x] 三构建零告警、发布包 17/17
- [x] 未改接口签名、未引三方依赖、未越文件域
- [x] 已知限制（分辨率不联动、注入代理验证）已写明
- [x] 四件套齐全；未自评为已验收