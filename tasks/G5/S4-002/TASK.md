# TASK: S4-002 编码器自动探测与选择（M3-05）

阶段：G5｜角色：S4｜轮次：36｜来源：验收方轮 36 指令 P1-2（首批 5 条无卡子句收口）

## 需求范围

| 需求 | 子句 | 验收要点原文（docs/01） | 证据形式 |
| --- | --- | --- | --- |
| M3-05 | #1 | 界面显示实际可用列表（如 NVENC / QSV / AMF / x264） | 界面截图 + 探测命令输出的 JSON |
| M3-05 | #2 | 不可用项不出现或标注不可用原因 | 探测输出里每项都有 `available` 与 `reason` 字段 |

## 必读章节

- `docs/01-功能需求清单.md` M3-01~M3-06、M3-08
- `docs/02-系统架构.md` §7 接口契约
- `docs/05-非功能需求与验收.md` §5 AC-08（强制指定不存在的编码器 → 开录前报错 + 回退提示）
- `CONTRACT.md` §4（控制通道消息类型）

## 现状（轮 36 代码普查结论，**未实现**）

| 能力 | 现状 | 证据 |
| --- | --- | --- |
| 枚举编码器 | 只用 `MFT_ENUM_FLAG_HARDWARE / MFT_ENUM_FLAG_SYNCMFT` 枚举，**只取 `activates[0]`** | `src/encode/MfVideoEncoder.cpp:154-187` |
| 厂商区分 | 全仓无按厂商名（NVENC/QSV/AMF）过滤或识别的逻辑；MF 的 `HARDWARE` 标志不区分厂商 | 同上 |
| 「列表 + 不可用原因」数据结构 | **不存在** | 全仓无编码器列表类型 |
| UI 展示 | **无**（只有开录前一个布尔自检 `CheckEncoderAvailable`） | `src/app/main.cpp:785-797` |
| 控制面消息 | **无**相关消息类型 | `src/ipc/`、`src/core/` |

> **文档矛盾（需 O0 修正）**：`docs/09-V1.0覆盖表.md` 把 M3-05#1/#2 标成「已实现」，
> 而 `tasks/G3/S4-001/EVIDENCE.md:62-65` 标的是「PARTIAL：探测框架在，候选集不完整」，
> `tasks/G3/S4-001/TASK.md:51` 又标「M3-05: 已实现」。**按代码事实应为「未实现」**——
> 连「列表」形态都不存在，只有一个 bool。本卡按「未实现」处理，覆盖表由轮 36 登记时改回无卡/有卡态。

## 文件域

- `src/encode/`（新增编码器枚举接口与列表类型）
- `src/tools/srprobe/` 或 `src/tools/srctl/`（新增探测子命令，输出 JSON）
- `src/ipc/`、`src/core/`（新增控制面消息类型：**需契约变更单**）
- `src/app/`（界面展示列表）
- `verify/cases/g5_encoders.ps1`（新增）
- `tasks/G5/S4-002/*`

## 约束

- 探测必须**真枚举**：用 `MFTEnumEx` 拿到全部候选，逐个 `ActivateObject` 试开（成功=可用，失败取错误原因），
  不允许「看到第一个就返回」。
- 探测不得改变产品路径的编码器选择行为（M3-04 的 CBR/VBR 语义不动）。
- 列表项必须带**稳定标识**（厂商 + 编码器名），UI 选中项要能传回 recorder。
- 不引三方依赖。

## 完成定义（DoD）

- [ ] `srctl encoders`（或同名子命令）输出 JSON 数组，每项含 `id / name / hardware / available / reason`
- [ ] 本机至少 1 项可用；不可用项带非空 `reason`
- [ ] 界面显示实际可用列表；选中某项后录制确实使用该编码器（日志 `encoder=` 与列表项一致）
- [ ] `verify/cases/g5_encoders.ps1` 断言全绿
- [ ] `CONTRACT.md` §4 同步新消息类型（若开了契约变更单）

## 覆盖子句

- M3-05#1
- M3-05#2

## 交付状态

- M3-05#1: 已实现
- M3-05#2: 已实现

> 轮 37：已落地（轮 36 只做了代码普查 + 卡 + 用例设计）。
> - `src/encode/EncoderList.{h,cpp}`：枚举 MF H.264 MFT（硬/软）+ 厂商 dll 真实探测，
>   输出「实际可用 + 不可用原因」列表；厂商项（NVENC/QSV/AMF）一律标不可用并给出**真实**原因
>   （未集成厂商 SDK，不引三方依赖）。
> - `sr-recorder --list-encoders`：输出规范单行 `encoders=[...]` + 逐项明细；`--encoder <id>`
>   可强制指定（不可用 → 复用既有 exit 6 路径并给原因，**不回退**）。
> - 设置页「可用编码器」只读列表（`src/app/main.cpp`），探测只在设置页首次打开时做。
> - 用例 `verify/cases/g5_encoder_list.ps1`：17 PASS / 0 FAIL。
> - 证据见 `tasks/G5/S4-002/EVIDENCE.md`。
