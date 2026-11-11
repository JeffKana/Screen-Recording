# TASK: S2-003 轮 42 · 为 M6-08#2 提供 CPU 中转的强制入口（接口侧）

阶段：G5｜角色：S2（采集与合成）｜轮次：42｜来源：同轮 S1-010 的跨域依赖

## 需求范围

配合 `tasks/G5/S1-010` 完成 `docs/01:203` M6-08#2「如无法避免则走 CPU 中转仍能录制成功」。
本卡只改 S2 域的一个头文件，不加逻辑、不改 `.cpp`。

## 文件域（白名单）

- `src/compositor/Nv12Converter.h`（`CONTRACT.md §2` 的 S2 域）
- `tasks/G5/S2-003/*`（本卡四件套）

> `src/recorder/`（S1 域）由 S1-010 独占，`src/app/`（S5 域）由 S5-005 独占，
> `verify/` + `docs/`（V1 域）由 V1-003 独占；三卡与本卡目录互斥。

## 上游交付

- `src/compositor/Nv12Converter.cpp:492-519`：既有 CPU 中转兜底（运行期 GPU 失败会置 `force_cpu_=true`）。
- `Nv12Converter.h:92`：`bool force_cpu_ = false`（私有），运行期降级用的同一标志。

## 约束

1. 只加一个 `public` 内联方法 `ForceCpu()`，把既有的私有标志置真；**不新增转换算法**、不改任何 `.cpp`。
2. 不引三方依赖、不改 `CONTRACT.md`（无接口签名变更，只是新增一个可选入口）。
3. 本卡不声明覆盖子句：子句由 S1-010 声明并给行为证据（避免「最保守」合并规则把新卡拖成「有卡未实现」）。

## 完成定义（DoD）

- [ ] `ForceCpu()` 在 `SR_TEST_HOOKS=ON/OFF` 三构建都零告警编译通过
- [ ] S1-010 的 `--force-cpu` 实测 `converter=cpu` + 录制成功 + 成片可解码
- [ ] 四件套齐全

## 覆盖子句

无（本卡只承接 S2 域的头文件，子句由 `tasks/G5/S1-010/TASK.md` 声明）。

## 交付状态

无（本卡不覆盖需求子句）。
