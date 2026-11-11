# SELFTEST: S1-010

## 0. 开工四项自查
| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01:203` M6-08 验收要点第 2 句 |
| 必读章节可定位 | 通过 | `CONTRACT §2/§18.2`、`docs/06 §4.2` |
| 上游交付物可获取 | 通过 | `src/compositor/Nv12Converter.cpp`、三套构建缓存 |
| 文件域无交集 | 通过 | 本卡 `src/recorder/`；`src/compositor/Nv12Converter.h` 由 S2-003 独占 |

## 1. 自测命令与输出
| 命令 | 结果 |
| --- | --- |
| `cmake --build build-msvc --config Release` | exit=0 warnings=0 |
| `cmake --build build-msvc --config Debug` | exit=0 warnings=0 |
| `cmake --build build-mingw` | exit=0 warnings=0 |
| `pwsh -File verify/tools/build_release.ps1` | 17/17，发布 help 无调试开关 |
| `pwsh -File verify/cases/g5_r42_closeout.ps1`（M6-08#2 段） | 3/3 PASS |

## 2. 红-绿对照
| 断言 | 旧值 | 新值 |
| --- | --- | --- |
| `converter=cpu` | 无稳定触发入口，只能靠运行期 GPU 失败偶发降级 | `--force-cpu` 稳定得到 `converter=cpu` |
| 对照 | — | 不传开关 `gpu-videoprocessor` |

## 3. 回归
全量回归见主卡 `artifacts/r42-regression.txt`；本卡改动不影响默认路径（对照断言可证）。

## 4. DoD 勾选
- [x] 开工四项自查通过
- [x] 一条需求一条证据，含命令与实测值
- [x] 对照组证明开关确实切换路径
- [x] 三构建零告警、发布包无调试开关
- [x] 未改接口签名、未引三方依赖、未越文件域
- [x] 四件套齐全；未自评为已验收