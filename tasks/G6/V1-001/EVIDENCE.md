# EVIDENCE: V1-001（G6 首版 zip）

## 1 · 发布构建 + 打包 + 自检

```powershell
pwsh -NoProfile -File verify/tools/build_release.ps1
```

```text
--- 1 发布构建（SR_TEST_HOOKS=OFF / SR_BUILD_TOOLS=OFF）---
  PASS release-bin-exists — 产物目录 D:\develop\project\person\Screen Recording\build-release\bin\Release
--- 2 产物白名单自检 ---
  PASS release-only-product-exe — 产物 exe：sr-recorder.exe,sr-ui.exe（期望只有 sr-ui.exe / sr-recorder.exe）
  PASS release-no-debug-tools — 调试工具存在性检查：srctl/srprobe 都不在包里（当前：sr-recorder.exe,sr-ui.exe）
  PASS release-help-no-debug-switch — --help 里残留的调试开关：无（期望 0）
  PASS release-version-ok — --version 输出：sr-recorder 0.1.0 (ipc protocol v1, build release, test_hooks=0, core state table 15 transitions)
--- 3 打包 ---
  PASS zip-no-pdb — 包内 pdb 数：0（期望 0）
  PASS zip-no-verify-docs — 包内 verify/docs 条目数：0（期望 0）
  PASS zip-has-install-scripts — 包内条目：ScreenRecorder-0.1.0-win-x64/install.ps1, ScreenRecorder-0.1.0-win-x64/README.txt, ScreenRecorder-0.1.0-win-x64/uninstall.ps1, ScreenRecorder-0.1.0-win-x64/VERSION.txt, ScreenRecorder-0.1.0-win-x64/bin/sr-recorder.exe, ScreenRecorder-0.1.0-win-x64/bin/sr-ui.exe；缺：无
--- 4 解压即用验证 ---
  PASS extract-runnable — 解压后：…\ScreenRecorder-0.1.0-win-x64
  PASS extract-version-ok — 解压后 --version：sr-recorder 0.1.0 (ipc protocol v1, build release, test_hooks=0, core state table 15 transitions)
  PASS extract-help-no-debug — 解压后 --help 无调试开关
  PASS extract-ui-runs — 解压后 sr-ui --version 退出码=0（期望 0）

RESULT: 12 PASS / 0 FAIL ; 包 D:\develop\project\person\Screen Recording\dist\ScreenRecorder-0.1.0-win-x64.zip（330,966 字节）；摘要 …\dist\release-summary.json
```

**产物**：`dist/ScreenRecorder-0.1.0-win-x64.zip`（330,966 字节）+ `dist/release-summary.json`（机器可读摘要）。

**包内结构**：

```text
ScreenRecorder-0.1.0-win-x64/
├── install.ps1              # 安装：建目录、拷文件、开始菜单快捷方式、HKCU 卸载登记
├── uninstall.ps1            # 卸载：删登记/快捷方式/程序目录（保留用户成片）
├── README.txt               # 安装步骤、系统要求、已知限制
├── VERSION.txt              # 版本号 + 构建 SHA + 构建时间 + 构建开关
└── bin/
    ├── sr-ui.exe
    └── sr-recorder.exe
```

**「调试开关不在包里」的判据**：`sr-recorder --help` 里**不出现**
`--inject-drop-every` / `--fail-write-after` / `--fail-encoder` / `--preflight-fail` /
`--fallback-dir` / `--diag-window-test`（`SR_TEST_HOOKS=OFF` 的构建整段调试帮助都不编译进去）✓。

## 2 · 安装 / 卸载往返实测

```powershell
$pkg = <解压后的包目录>
pwsh -File "$pkg\install.ps1" -InstallDir "<临时目录>\installed"
pwsh -File "<临时目录>\installed\uninstall.ps1" -InstallDir "<临时目录>\installed"
```

```text
--- install ---
已安装到 …\sr-install-test2\installed（sr-ui.exe, sr-recorder.exe）
已创建开始菜单快捷方式
已登记卸载项
安装完成。启动：…\sr-install-test2\installed\bin\sr-ui.exe
--- files ---
README.txt, uninstall.ps1, VERSION.txt, sr-recorder.exe, sr-ui.exe
--- uninstall ---
已移除卸载登记
已移除开始菜单快捷方式
已删除程序目录 …\sr-install-test2\installed
卸载完成。
residual=False
```

| 检查 | 结果 |
| --- | --- |
| 安装后文件齐全 | `sr-ui.exe` / `sr-recorder.exe` / `uninstall.ps1` / `VERSION.txt` / `README.txt` ✓ |
| 开始菜单快捷方式 | 已创建（`%APPDATA%\Microsoft\Windows\Start Menu\Programs\ScreenRecorder.lnk`）✓ |
| HKCU 卸载登记 | 已写入 `HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\ScreenRecorder` ✓ |
| 卸载后残留 | 程序目录 `residual=False` ✓；注册表项与快捷方式已删 ✓ |
| 用户成片 | **保留**（卸载脚本默认不删；`-Purge` 才删）✓ |

## 3 · 未做的部分（诚实标注）

| 项 | 状态 | 原因 |
| --- | --- | --- |
| 兼容性矩阵（`docs/11` §6） | 未做 | 需要多台真机（不同 GPU/DPI/系统版本），本机只有虚拟显示 |
| 代码签名 | 未做 | 需要购买证书（外部依赖），V1.0 用 zip + 明确说明 |
| 安装到真实用户目录 | 未做 | 本轮在临时目录做往返，避免污染本机 `%LOCALAPPDATA%` 的真实安装 |
| `sr-ui --version` 的文字输出 | 不可见 | `sr-ui` 是 GUI 子系统程序，stdout 不接控制台；改用**退出码**判定（`extract-ui-runs`） |
