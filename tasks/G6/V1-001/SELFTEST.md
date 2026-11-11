# SELFTEST: V1-001（G6 首版 zip）

## 0. 开工四项自查（06 §4.2）

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | ✅（无需求子句） | 交付产物类工作；G6 门判据在 `docs/08` §3 |
| 必读章节可定位 | ✅ | `docs/11`（轮 36 出方案）、`docs/08` §3（G6 判据）、轮 37 指令 §1/§4.4 —— 均存在 |
| 上游交付物可获取 | ✅ | `docs/11` 的形态选型与白名单清单；`build-msvc` 的产品二进制 |
| 文件域无交集 | ✅ | 只动 `verify/tools/build_release.ps1`、`packaging/`、`dist/`；与本轮并行的 `tasks/G5/V1-002`（verify/tools 的其它文件 + docs）不重叠 |

## 1. 命令与输出

```powershell
pwsh -NoProfile -File verify/tools/build_release.ps1
```

```text
RESULT: 12 PASS / 0 FAIL ; 包 dist\ScreenRecorder-0.1.0-win-x64.zip（330,966 字节）；摘要 dist\release-summary.json
```

安装/卸载往返（见 EVIDENCE §2）：安装 → 文件齐全 + 快捷方式 + 卸载登记；卸载 → `residual=False`。

## 2. 回归

| 项 | 结果 | 说明 |
| --- | --- | --- |
| `build_release.ps1` | 12/12 PASS | 发布构建 + 白名单 + 打包 + 解压验证 |
| 安装/卸载往返 | PASS | 临时目录内实测 |
| `build-msvc`（验收构建） | 未受影响 | 发布构建用**独立目录** `build-release`，`SR_TEST_HOOKS=ON` 的验收构建不动 |
| `ui_smoke` ×3 / `g5_format` / `g4_gate` | 见 `tasks/G5/S4-002` 的 SELFTEST | 产品代码本轮未因打包改动 |

## 3. 交付前自检

- [x] 开工四项自查全部通过并已记录
- [x] 一条命令可重跑（`build_release.ps1`），输出机器可读摘要 JSON
- [x] Release + Debug 零告警（`build-msvc` 侧；发布构建同样零告警）
- [x] 文件域无越界；`vcpkg.json` 未改；`verify/cases/` 未改（只新增 `verify/tools/build_release.ps1`）
- [x] HANDOFF 四问齐答
- [x] `## 交付状态` 已写（明确标注「首版，非发布版」）
- [x] 已知限制已写明（兼容性矩阵未做、无签名、GUI 子系统的 stdout 不可见）
