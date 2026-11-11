# EVIDENCE: S4-001 兜底提交与后端降级（T-16b）

锚点：M8-06（路径/权限类失败的后半段）、M8-02（恢复命名）、C11（恢复期后端降级）、M8-08（编码器全失败）。

复现：

```powershell
pwsh -NoProfile -File verify/cases/g5_fallback.ps1 -BuildDir build-msvc -Config Release   # 19/19
pwsh -NoProfile -File verify/cases/g5_inject.ps1   -BuildDir build-msvc -Config Release   # 28/28
```

## A 写失败 → 兜底提交

| 判据 | 实测 |
| --- | --- |
| 退出码 | `record_exit=16` |
| 兜底路径 | `fallback\rec.mkv.recovered.mkv` |
| 可播 | 解码零错误，含音轨 |
| 时长 | **6.41s**，已落盘末尾 PTS = 6408ms，下界 4.91s |
| 原目录 | 条目数 **0**，无 0 字节文件，无成品 |

## B 后端真实降级（C11）

`--fail-resume-backend` 会在恢复时把原后端写入 `CaptureRequest::excluded_backend`，
由 `CaptureChain` 在**枚举阶段跳过**该后端 —— 这走的是真实降级链，不是事后改写标记。

```
[record] WARNING: backend changed after resume: wgc -> dxgi
record_exit=0  frames_encoded=180  backend=dxgi  backend_changed=1  duration=5.999
```

## C 恢复产物命名（D-32）

| 判据 | 实测 |
| --- | --- |
| 首次扫描 | `repairable=1` |
| 首次修复 | 产出 `rec.mkv.recovered.mkv`，源 `.part` 改名 `.part.repaired` |
| 二次扫描 | **`scanned=0`**（不再重复提示） |
| 二次修复 | 产出 `rec.mkv.recovered_001.mkv`，首个产物完好 |

## D 编码器全失败（F2 整改）

注入点从 `RecordingSession` 下沉到 `MfVideoEncoder::CreateTransform`：

```
record_exit=6
error=injected: software encoder unavailable (injected: hardware encoder unavailable)
```

错误信息里同时出现 hardware 与 software 两条，说明「硬编创建失败 → 软编创建失败 → 报错」
三段真实分支都被执行，而非调用方自造字符串。

## 本轮修掉的缺陷

| 编号 | 现象 | 根因 |
| --- | --- | --- |
| D-32 | 修复产物覆盖、`.part` 不消解、二次扫描重复提示 | `Repair` 用 `ios::trunc`；成功后未改名源文件 |
| D-33 | 兜底后原目录残留 `.part.repaired` / 无法删除 | `MkvWriter::Close` 在 flush 失败时提前 return，没关文件句柄 |

D-33 是在验证 P0-2 时暴露的：`std::filesystem::remove` 静默失败，回头查才发现句柄没关。
它是「错误路径上的资源释放」这类典型遗漏 —— 正常路径永远测不到。

## 回归

`g4_control` 32/32、`g5_inject` 28/28、`g3_output` 15/15、`g3_storage` 11/11、`g3_recovery` 9/9、
契约 7/7；MSVC 与 MinGW 均零告警。

> 回归中曾出现 `g4_control` 3 项失败：兜底分支误伤了 `--fail-resume`（数据完好但运行期出错）。
> 已按「数据完整性受损才走兜底」区分，见 `RecordingSession::Run` 的 `integrity_lost`。

## 未覆盖

- ER-08 TDR（V1.5）
- 控制通道 `--control-pipe`（T-09 前置）
