# EVIDENCE: S2-003（Nv12Converter::ForceCpu 接口侧）

> 口径：执行方证据，非验收结论。数据 2026-11-09。

## 配合 M6-08#2 的 S2 域改动

- 改动范围：仅 `src/compositor/Nv12Converter.h` 加一个 `public` 内联方法：
  ```cpp
  void ForceCpu() { force_cpu_ = true; }
  ```
  `src/compositor/Nv12Converter.cpp` **一字未改**：`Resize()` 原本就以 `force_cpu_`
  决定是否建 GPU 资源，运行期 GPU 失败路径也置同一个标志。
- 命令：`cmake --build build-msvc --config Release/Debug`、`cmake --build build-mingw`
- 输出：`tasks/G6/V1-003/artifacts/build-summary.txt` — 三构建
  `exit=0 warnings=0 errors=0`。
- 行为证据（由 S1-010 消费）：`tasks/G6/V1-003/artifacts/g5_r42_closeout.ps1.txt`
  的 `m6-08-02-force-cpu-path` → `converter=cpu`、`record_exit=0`、成片可解码。
- 文件域：`src/compositor/` 属 `CONTRACT §2` 的 S2 域；本卡独占该头文件，
  S1-010 卡的 `## 文件域` 已去掉它（首轮建卡时误声明，发现后按「文件域无交集」纠正）。

## 已知限制
- 本卡只提供入口，不含独立行为断言；子句证据在 S1-010。
- 执行方不自评为已验收。