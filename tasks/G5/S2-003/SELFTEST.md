# SELFTEST: S2-003

## 0. 开工四项自查
| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | 配合 M6-08#2，`docs/01:203` |
| 必读章节可定位 | 通过 | `CONTRACT §2`、`docs/06 §4.2` |
| 上游交付物可获取 | 通过 | `Nv12Converter.h/.cpp` |
| 文件域无交集 | 通过 | 本卡仅 `src/compositor/Nv12Converter.h`；S1-010 已去掉该文件 |

## 1. 自测命令与输出
| 命令 | 结果 |
| --- | --- |
| 三构建编译 | 全部 exit=0 warnings=0 errors=0 |
| `pwsh -File verify/tools/build_release.ps1` | 17/17（发布构建含该头文件） |
| `pwsh -File verify/cases/g5_r42_closeout.ps1` | M6-08#2 段 3/3 PASS |

## 2. 红-绿对照
| 断言 | 旧值 | 新值 |
| --- | --- | --- |
| CPU 路径可达 | 无公开入口 | `ForceCpu()` + `--force-cpu` → `converter=cpu` |

## 3. 回归
见主卡 `artifacts/r42-regression.txt`。

## 4. DoD 勾选
- [x] 开工四项自查通过
- [x] 只改一个头文件、未改 `.cpp`、未加新算法
- [x] 三构建零告警、发布包 17/17
- [x] 文件域纠偏已留痕（S1-010 去掉 compositor）
- [x] 无接口签名变更、未引依赖
- [x] 四件套齐全；未自评为已验收