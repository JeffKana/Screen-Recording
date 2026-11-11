# HANDOFF: S1-007（轮 33）

## 1. 我改了什么接口？

- **`RecorderController::Start` 增加两个参数**：`uint32_t fps`、`const std::wstring& rate_mode`。
  调用点只有 `src/app/main.cpp` 一处；命令行仍走 `--fps/--rate-mode`，IPC 协议未改。
- **`sr-recorder` 新增 CLI**：`--max-bitrate <kbps>`、`--quality <0~100>`；
  `--rate-mode` 现在真的分派 `cbr|vbr`（未知值仍按 CBR，未改旧语义）。
- **新增 stdout 字段**：`rate_control_quality`、`rate_control_max_kbps`
  （既有字段语义未改，`rate_control` 取值新增 `vbr`）。
- **INI 新增 `[video] fps` / `[video] rate_mode`**；新增 `SaveVideoSettings()`。
- **新增 `src/app/FileOps.{h,cpp}`**：`RenameRecording` / `DeleteToRecycleBin` / `RevealInExplorer` /
  `CopyPathToClipboard`；`sr-ui` 的 CMake 目标加了该源文件，未加新依赖（仍是系统库）。
- **UI 列表**：改为多选 + `LVS_EDITLABELS` + 右键菜单（重命名/删除/打开目录/复制路径）；
  新增测试钩子 `--set-fps` / `--set-rate-mode` / `--list-op` / `--list-from` / `--list-to` / `--list-confirm`。
- **覆盖模型**：`## 交付状态` 的键支持 `M\d-\d{2}#\d+`，取「子句声明 > 需求级兜底」；
  `coverage_report.py --self-test` 增加正反两个用例。

## 2. 下游需要接什么？

- 设置面板新增了两行控件（帧率、码率模式）；如果 G6 做安装包/本地化，需要给
  「帧率」「码率模式」「CBR（固定码率）」「VBR（平均码率 + 峰值上限）」补字符串。
- 画质档位只有 CLI/INI 之外的录制参数，没有 UI 入口；R3 已证明该 MFT 下无可测差异，
  下游不要把它当画质卖点。
- 8h 长录制由 `tasks/G5/S6-002` 接手：`g5_lt01_real.ps1` 已启动，读取
  `verify/artifacts/lt01-real-*/summary.json` 即可判定；不要再用 6 段代理替代。
- `docs/09` 的挂账理由列仍是人工维护；下次重生成前先改理由，再 `--write`。

## 3. 已知限制是什么？

1. **VBR 在静态桌面显著欠码**：实测 avg 3,037kbps / 偏差 49%。VBR 不补 filler，
   预算填不满就低于标称；`g5_format` 只在运动源上判 ±20%。如果产品要求「任何内容都贴标称」，
   应改用 CBR，而不是给 VBR 加 filler。
2. **画质档位无可测效果**：`CODECAPI_AVEncCommonQuality` 70/85 均可回读，但成片
   PSNR/SSIM 完全一致（33.78dB / 0.9935）。D-53 的稳定化归因需要修正为
   mean=max + 0.1s buffer + filler。
3. **回收站校验依赖 `$I` 记录**：不同 Windows 版本的 `$I` 布局可能变化；本轮按
   「8B 头 + 8B 大小 + 8B 时间 + 4B 长度 + UTF-16 路径」解析，已在 Win11 26100 验证。
4. **UI 重命名走 `ListView_EditLabel`**：在 `LVN_ENDLABELEDIT` 里不能直接刷新列表，
   已改为 `PostMessage(WM_APP+1)` 异步执行；如果下游要加「重命名前校验扩展名」，
   改 `FileOps::RenameRecording` 一处即可。
5. **`ui_smoke` 在高负载下有偶发**：`region-overlay` / `preflight-display` 两项在机器忙时
   会因输入事件延迟失败，清理残留进程后复跑 101/101；不是产品回归，但验收时应先
   `Get-Process sr-ui,sr-recorder | Stop-Process`。

## 4. 哪些需求我**没**做完？

- **M6-02#1 / M6-09#1（8h 单文件）**：不在本卡，`tasks/G5/S6-002` 的 `g5_lt01_real.ps1`
  已启动真实 28,800s 运行，交付时仍在录制，**没有结论**；G5 不得因此关闭。
- **M3-04 的 UI 画质档位**：没做（模式入口已做；画质档位经 R3 证明无可测差异）。
- **M5-07 的「打开所在目录 / 复制路径」自动化断言**：功能已实现，用例未覆盖；
  本轮验收只要求重命名/删除/批量确认。
- **R3 的「70 比 85 差」结论**：没得到，实测无差异；如果验收要求「85 必须更好」，
  需要换编码器或换测量内容，当前 MFT 做不到。
