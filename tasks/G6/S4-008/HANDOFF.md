# HANDOFF: S4-008（轮 47 · M3-05 列表一致性产品实现）

## 1. 我改了什么接口？

新增的是 `src/encode` **模块内部 API**，既有签名与行为不变，`CONTRACT.md` 未改、未开契约变更单
（没有新 IPC 消息、状态机转移或退出码；`exit 6` 仍复用 `CONTRACT §18.2`）。

| 文件 | 变化 | 下游影响 |
| --- | --- | --- |
| `src/encode/EncoderList.h` | 新增 `EncoderProbeOptions`（`fail_hardware` / `fail_software`）、`EncoderLookup`、`ListEncoders(const EncoderProbeOptions&)` 重载、`LookupEncoder(id)`；`ListEncoders()` / `FormatEncoderList()` / `FindEncoder()` 原样保留 | 列表探测可传入定向注入；录制启动路径改用 `LookupEncoder` 做静态映射，**不再跑整表探测**（消除 MF 启停嵌套） |
| `src/encode/EncoderList.cpp` | `MakeMf()` 由「`MFTEnumEx` + `ActivateObject` 成功即 available」改为调用 `MfVideoEncoder::ProbeAvailable()`；厂商项改为共享 `VendorSpec` / `MakeVendor()`；删掉本文件自带的 `MFStartup/MFShutdown` | 「列表说可用」与「产品打不开」由同一份实现决定，构造上不会分叉 |
| `src/encode/MfVideoEncoder.h/.cpp` | 新增 `static ProbeAvailable(bool hardware, const EncoderProbeOptions&, std::string&)`、`TryEncodeOneFrame()`、`SetFailureInjection()`；成员新增 `fail_hardware_encoder_` / `fail_software_encoder_` / `com_owned_`，启用原本闲置的 `mf_started_` | 探测走与录制完全相同的 `Open`（创建→协商→码控）并试编码取回一帧；**失败分支也** `MFShutdown()` / `CoUninitialize()` |
| `src/encode/MfVideoEncoder.cpp::Open` | `requested_id_` 非空时：用 `LookupEncoder` 静态映射；`Negotiate` / `ApplyRateControl` 两处**禁止硬编→软编回退**，统一报 `"encoder '<id>' unavailable: <原因>"` 走 `exit 6` | 自动选择（未点名）的 M3-06 回退语义**完全不变**；被点名的编码器失败即失败，不再静默换软编 |
| `src/recorder/main.cpp` | `--fail-encoder` 增加可选 `hardware|software|all`（**无参仍是全部失败**）；新增 `--probe-repeat <n>`（SR_TEST_HOOKS）；`RunListEncoders(const Options&)` 输出 `probe_iteration=` 状态/句柄行；`Options` 增两字段 | 发布包（`SR_TEST_HOOKS=OFF`）不解析、不显示这些调试开关；UI 设置页仍 spawn `--list-encoders`，输出格式未变 |
| `src/recorder/RecordingSession.h/.cpp` | `Options` 增 `fail_hardware_encoder` / `fail_software_encoder`；调用 `encoder.SetFailureInjection(...)` | 其它录制选项与状态机不变 |

## 2. 下游需要接什么？

1. **验收方**：按 `.codebuddy/plans/轮47指令.md` §通过判据独立重跑；命令见
   `SELFTEST.md §1` / `tasks/G6/V1-020/SELFTEST.md §1`。红端可用
   `g5_encoder_consistency.ps1 -RecorderPath <轮 46 二进制>` 复现。
2. **用例维护者**：`g5_closeout.ps1:287` 把「不可用行 == 3」写死，轮 47 诚实列表为 4
   （新增 `mf-h264-hw unavailable`），附加抽样 `17 PASS / 1 FAIL`。本卡文件域不含该用例，
   未改、未放宽；建议把断言改为「三厂商 id 各命中且各自 `available=0` + 非空 reason」，
   由验收/规划侧决定是否单独开卡。
3. **矩阵维护者**：`docs/14` 硬编抽测格现在是「不可用路径实测、不可计硬编 PASS」；
   有真可用硬编 MFT 的机器才能补三件套，别用轮 46 的软编 6/6 冒充硬编。
4. **规划侧**：轮 48 `S4-006` / 轮 49 `S4-007` 见 `docs/19 §0.B`；发布复核轮见 `docs/22`。
5. **以后改编码器选择逻辑**：先跑 `g5_encoder_consistency.ps1`；任何「静默回退」都会让
   `r47-forward-available-items` / `r47-no-silent-fallback` 转红。

## 3. 已知限制是什么？

- **本机硬件 MFT 不可用**：真激活在协商阶段被拒（`encoder rejected NV12 input type`），
  因此 `mf-h264-hw` 标 `unavailable`、强制 `exit 6`。修复的是反馈一致性，不是硬编能力。
- **探测器不创建 D3D device / processor**：产品编码路径只吃 CPU NV12，探测器同样不碰 D3D，
  D-25（转换器绑在已释放设备）无泄漏面；COM 用 `win::ComPtr`，MF/COM 启停成对（含失败分支）。
  跳过硬件 MFT 激活时，同进程 5 轮探测句柄 `204→204`（增长 0）。
- **第三方 MFT 激活的驱动侧句柄**：同进程真实激活硬件 MFT 有 **+3 句柄/次**
  （5 轮 268→280），`ShutdownObject()` / `CoFreeUnusedLibraries()` / 独立 COM 单元线程都不能消除，
  属第三方 `ActivateObject` 内部行为、进程退出即回收。产品每次探测都在一次性子进程完成
  （UI 设置页 spawn `sr-recorder --list-encoders`），不会在长驻进程累积；用例只做上限护栏。
- **探测配置固定**：1280×720@30 / 6000kbps CBR，与 `--record` 默认档同量级，
  不覆盖所有分辨率/帧率/码率组合。
- **单秒过冲方差**：`g5_format` 首次 14/1（12,799kbps），复跑 15/0；与轮 44/45 同源，未改判据。
- **降级验证**：本机不是 `docs/05 §1` 的 B 机；矩阵单屏格用 `--display 0` 代表单屏。

## 4. 哪些需求我没做完？

- **M3-06#1「硬编优先、软编确定性回退」未改**：自动路径保持原状，本卡只收紧被点名的路径；
  没有实现「界面提示已回退」的新工作。
- **硬件编码能力本身未实现**：本机的硬件 MFT 仍不能用；NVENC / QSV / AMF 厂商 SDK 仍未集成
  （`available=0` + 真实 dll 原因），M6-04#1 需真机。
- **M3-09#1/#2/#3 未实现**（MP4 muxer / MKV 多音轨 / 格式切换），排轮 48–49。
- **M1-27#1、M6-06、LT/ER 三次、兼容性矩阵、B 机性能基线**：本卡未涉及，见 `docs/22`。
- **P1 硬编矩阵格没有转 PASS**（无硬编成片、三件套不可执行）。
- **附加抽样 `g5_closeout` 17/1**：用例侧过期断言未修（无授权），见 §2 #2。
- **验收结论**：本卡不写。M3-05#1 是否判 PASS 由验收方独立重跑后给。

## 2.1 交付通道状态（如实记录，非验收结论）

- 分支：`round/47`，从 `main` @ `560087a` 拉出；PR 与 CI 结果由主卡 `V1-020` 的第二个提交补记。
