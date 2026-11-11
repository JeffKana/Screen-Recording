# HANDOFF: V1-001（G6 首版 zip）

## 1. 我改了什么接口？

**没有改任何产品接口**（`src/` 一行未动）。新增的是**交付链路的脚本**：

| 文件 | 作用 |
| --- | --- |
| `verify/tools/build_release.ps1` | 发布构建（`-DSR_TEST_HOOKS=OFF -DSR_BUILD_TOOLS=OFF`）+ 产物白名单自检 5 条 + 打包 + 解压即用验证 4 条 + `dist/release-summary.json` |
| `packaging/install.ps1` | 免管理员逐用户安装：拷文件、开始菜单快捷方式、HKCU 卸载登记 |
| `packaging/uninstall.ps1` | 卸载：删登记/快捷方式/程序目录；**默认保留用户成片**（`-Purge` 才删） |
| `packaging/README.txt` | 安装步骤、系统要求、已知限制 |

**新增的产品命令不是新增接口**：`sr-recorder --list-encoders` / `--encoder <id>` 在
`tasks/G5/S4-002` 已记录；本卡只是把它们打进发布包。

## 2. 下游需要接什么？

| 下游 | 要做什么 |
| --- | --- |
| 验收方 | 复跑 `pwsh -NoProfile -File verify/tools/build_release.ps1`（期望 12 PASS / 0 FAIL）；产物 `dist/ScreenRecorder-0.1.0-win-x64.zip` |
| 验收方 | 解压后手测：装 → 启动 `sr-ui.exe` → 录 60s → 成片可播 → 卸载无残留 |
| 轮 40（发布复核） | 补 `docs/11` §6 的兼容性矩阵（要多台真机）+ `docs/05` §7.2 逐条复核 + 全片解码（新判据） |
| 商业决策 | 代码签名证书（外部依赖，V1.0 未做） |

## 3. 已知限制是什么？

1. **这是「首版」，不是发布版**：没签名、没做兼容性矩阵、没在干净机/干净用户账号上实测安装
   （本轮在临时目录做往返，避免污染本机 `%LOCALAPPDATA%` 的真实安装）。
2. **`sr-ui --version` 的文字看不到**：`sr-ui` 是 GUI 子系统程序，stdout 不接控制台；
   用例改用**退出码**判定（`extract-ui-runs` = 0）。验收方若要核对版本号，看包里的 `VERSION.txt`。
3. **无三方依赖**：`WScript.Shell`（快捷方式）、`Compress-Archive`（打包）都是系统自带 ✓；
   没有 WiX/NSIS，因此**没有「应用和功能」里的完整条目**（只写了 HKCU 卸载登记项，够卸载用）。
4. **`build-release` 与 `build-msvc` 必须并存**：验收用例依赖 `SR_TEST_HOOKS=ON`，
   发布包必须 `OFF`；两者不可互相替代（`docs/11` §4 已写明）。
5. **每次打包都会重新配置并构建** `build-release` 目录（首次约 2~4 分钟）；
   `-SkipBuild` 可跳过构建直接打包（改完代码后别用）。

## 4. 哪些需求我**没**做完？

| 项 | 状态 | 没做完的部分 |
| --- | --- | --- |
| G6 首版 zip | **已实现** | 无（12/12 PASS + 安装/卸载往返实测） |
| 兼容性矩阵（`docs/11` §6） | 未做 | 需要多台真机（不同 GPU / DPI / Win 版本），本机只有虚拟显示 |
| 代码签名 | 未做 | 需购买证书（外部依赖，商业决策） |
| 「干净机安装 → 录 60s → 成片可播」的整机验收 | 未做 | 本轮只验到「解压即用 + 安装/卸载往返」，整机链路留给轮 40 |
| M1-08/09、M1-27 的实现 | 未做 | 见 `tasks/G5/S2-002`、`tasks/G5/S5-003`；本轮 P1 的「2 条无卡」只落了 M5-04 |
