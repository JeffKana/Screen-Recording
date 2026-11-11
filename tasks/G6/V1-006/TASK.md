# TASK: V1-006 轮 42 · M6-10#2 HVCI 环境实测收口

阶段：G6｜角色：V1（验收侧）｜轮次：42｜来源：`.codebuddy/plans/轮42指令.md` §1 P0

> 本卡是**零代码改动的环境实测型收口**：产品是纯用户态（M6-10#1 已实现：
> 安装包无驱动组件），本机恰好是 HVCI 已启用的 Win11，因此可以直接在真实环境
> 验证「可正常安装运行」，不需要模拟或注入。

## 需求范围

| 子句 | 验收要点原文（`docs/01:205`） |
| --- | --- |
| M6-10#2 | 在启用「内存完整性（HVCI）」的 Win11 上可正常安装运行 |

## 文件域（白名单）

- `verify/cases/g5_r42_hvci.ps1`（新增，专属行为断言）
- `docs/14-轮41-兼容性矩阵草案.md`（矩阵 Win11 100% 单屏格填实测值）
- `tasks/G6/V1-006/*`（本卡四件套）

> 与 `tasks/G6/V1-003`（其它 verify/ 用例 + docs/ 记录）、`tasks/G5/S1-010`、
> `tasks/G5/S2-003`、`tasks/G5/S5-005`（src/ 实现）、`tasks/G5/V1-005`（状态迁移）目录互斥。

## 上游交付

- 轮 40 `tasks/G6/V1-001` 发布包与 `dist/ScreenRecorder-0.1.0-win-x64.zip`
- `packaging/install.ps1`（`-InstallDir` / `-Quiet` 参数）
- `verify/cases/g5_closeout.ps1:155-180` 的 M6-10#1 断言（无驱动组件）

## 约束

1. 环境判据必须**真实读取** `HKLM:\...\HypervisorEnforcedCodeIntegrity` 的 `Enabled=1` 与 OS Caption；
   不是 HVCI 环境就 SKIP，不得用模拟冒充实测。
2. install.ps1 用真实参数 `-InstallDir` 装到临时目录，不改用户环境；断言退出码 0 + 安装产物存在。
3. 包内 exe 在 HVCI 环境下真实录 3s，断言 `record_exit=0` + 成片 ffmpeg 解码零错误 + 双轨。
4. 不改 `src/`、不改 `CONTRACT.md`、不引三方依赖、不重跑 8h。

## 完成定义（DoD）

- [ ] `g5_r42_hvci.ps1` 全绿且环境判据为真实 HVCI
- [ ] 矩阵 1 格填实测值 + 日期，其余 10 格仍标未测
- [ ] 四件套齐全

## 覆盖子句

- M6-10#2

## 交付状态

- M6-10#2: 已实现（轮 42；本机 HVCI Enabled=1 的 Win11 上，发布包 install.ps1 安装成功 + 包内 exe 录制成功 + 成片可解码 + 无驱动组件；真实环境实测，非模拟）
