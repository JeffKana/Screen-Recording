# TASK: V1-001 G6 首版 zip（发布构建 + 打包 + 解压即用验证）

阶段：G6｜角色：V1（交付验证侧）｜轮次：37｜来源：验收方轮 37 指令 P1-2（G6 首版 zip）

## 需求范围

本卡不覆盖需求子句，是**交付产物**类工作（`docs/08` §3 的 G6 门判据：
「MSVC Release 安装包可安装运行；无阻塞级缺陷」）：

| 项 | 要求（验收方轮 37 指令 P1-2 + `docs/11`） | 判据 |
| --- | --- | --- |
| 发布构建 | `SR_TEST_HOOKS=OFF`（去掉验收注入开关）+ `SR_BUILD_TOOLS=OFF`（不编译调试工具） | `--help` 无调试开关；产物只有 2 个 exe |
| 包结构 | 只带 exe + 必要脚本 + 许可/说明；**不带** `.pdb` / `verify/` / `docs/` | zip 条目清单 |
| 解压即用 | 解压后 `--version` / `--help` 正常 | 解压目录里直接跑 |
| 安装/卸载 | 脚本化、零三方依赖、免管理员、卸载不删用户成片 | 往返实测 |

## 必读章节

- `.codebuddy/plans/轮37指令.md` §1（P1-2）、§3、§4.4（不打包 pdb/verify/docs）
- `docs/11b-轮36-G6交付方案.md` §2（形态选型）、§3（产物白名单）、§4（构建开关）、§5（安装/卸载/残留）、§9（下一步）
- `docs/08-阶段重排与任务清单.md` §3（G6 门判据）

## 文件域

- `verify/tools/build_release.ps1`（新增：发布构建 + 白名单自检 + 打包 + 解压验证）
- `packaging/install.ps1`、`packaging/uninstall.ps1`、`packaging/README.txt`（新增）
- `dist/`（产物，不入版本库）
- `tasks/G6/V1-001/*`

## 约束

1. **不引三方依赖**（轮 37 指令 §4.4 + `docs/11` §1）：只用 PowerShell 内置能力
   （`Compress-Archive` / `Expand-Archive` / `WScript.Shell` COM）。
2. **发布包不得含调试开关**：`SR_TEST_HOOKS=OFF` 的构建才允许进包。
3. **不打包** `.pdb` / `verify/` / `docs/` / 调试工具（srctl / srprobe）。
4. 发布构建目录与验收构建目录**分开**（`build-release` vs `build-msvc`）：验收用例依赖
   `SR_TEST_HOOKS=ON`，两者不可互相替代。

## 完成定义（DoD）

- [x] `verify/tools/build_release.ps1`：一次命令产出发布构建 + zip + 自检摘要（JSON）
- [x] 产物白名单自检 5 条全过（只有 2 个 exe / 无 srctl+srprobe / `--help` 无调试开关 / `--version` 含 `test_hooks=0` / 版本可追溯）
- [x] zip 不含 `.pdb` / `verify/` / `docs/`，含 `install.ps1` / `uninstall.ps1` / `README.txt` / `VERSION.txt`
- [x] 解压即用：解压后 `--version` / `--help` 正常、`sr-ui --version` 退出码 0
- [x] 安装/卸载往返实测：安装后文件齐全 + 快捷方式 + 卸载登记；卸载后无残留

## 覆盖子句

无（交付产物类工作；G6 的门判据在 `docs/08` §3，不在 V1.0 需求子句里）。

## 交付状态

- G6 首版 zip：已实现（首版，非发布版）
- 兼容性矩阵（`docs/11` §6）：未做（要真机）
- 代码签名：未做（外部依赖）
