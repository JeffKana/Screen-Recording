# EVIDENCE: V1-006（M6-10#2 HVCI 环境实测）

> 口径：执行方证据，非验收结论。数据 2026-11-09；`dist/ScreenRecorder-0.1.0-win-x64.zip`。

## M6-10#2 在启用内存完整性（HVCI）的 Win11 上可正常安装运行

- 验证方式：**真实 HVCI 环境**下跑发布包 install.ps1 + 包内 exe 录制，零代码改动、零模拟。
- 命令：`pwsh -File verify/cases/g5_r42_hvci.ps1`
- 输出：`tasks/G6/V1-003/artifacts/g5_r42_hvci.ps1.txt` — **7 PASS / 0 FAIL / 0 SKIP**：
  1. `m6-10-02-hvci-env`：OS=`Microsoft Windows 11 家庭版 中文版`，
     `HKLM:\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity`
     `Enabled=1`、`WasEnabledBy=2`。
  2. `m6-10-02-package-layout`：包内含 install.ps1 + bin/sr-recorder.exe + bin/sr-ui.exe。
  3. `m6-10-02-no-driver-payload`：包内 `.sys/.inf/.cat/.vxd/.drv` 文件数 = **0**
     （纯用户态，HVCI 不会拦未签名内核驱动）。
  4. `m6-10-02-install-runs-under-hvci`：`install.ps1 -InstallDir <tmp> -Quiet` 退出码 **0**，
     安装产物 `<tmp>/bin/sr-recorder.exe` 存在。
  5. `m6-10-02-record-runs-under-hvci`：包内 exe `--record --seconds 3` → `record_exit=0`，
     成片 2,326,391 B。
  6. `m6-10-02-decodable`：ffprobe duration=2.999s、video+audio 双轨、`ffmpeg -f null` 零错误。
  7. `m6-10-02-ui-runs-under-hvci`：包内 sr-ui `--version` 退出码 0 + 日志 `test_hooks=0`。
- 红-绿：首轮 `m6-10-02-ui-runs-under-hvci` FAIL，根因是 sr-ui 为 GUI 子系统、
  `--version` 写父控制台不进管道（用例取数缺陷）；改用退出码 + 日志判定后 PASS，**判据未放宽**。
- 关联：`g5_closeout.ps1` 的 M6-10#1 断言（无驱动组件）为同族旁证。

## 已知限制

- 环境判据是**真实读取**注册表与 OS Caption；非 HVCI 环境用例会 SKIP，不用模拟冒充实测。
- 只覆盖「可正常安装运行」，不涉及 150% DPI（M6-06 仍 BLOCKED）或其它 Windows 版本。
- 执行方不自评为已验收。