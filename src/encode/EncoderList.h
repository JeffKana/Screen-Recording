#pragma once

#include <string>
#include <vector>

namespace sr::encode {

// M3-05：实际可用的视频编码器列表。
//
// 需求原文（docs/01）：「界面显示实际可用列表（如 NVENC / QSV / AMF / x264）」、
// 「不可用项不出现或标注不可用原因」。
//
// 口径（B 方案，轮 37；轮 47 F1 修正「可用」的判定）：
//   * 列表 = 产品**真的能选**的编码器 + 探测到不可用的厂商编码器（带真实原因）。
//     不可用项不消失——用户看不到「为什么没有硬编」就会去报 bug。
//   * available=1 的唯一含义是「本产品现在真的能用它编码」：探测走 MfVideoEncoder
//     的同一条 Open + 试编码一帧路径（创建 MFT → 协商 NV12/H.264 → 码控 → 一帧 NV12），
//     不再把 ActivateObject 成功当成可用（轮 46 验收 F1：列表说可用、开录却静默换软编）。
//   * available=0 必带 reason，写明失败阶段；厂商项原因来自 dll 是否真实存在。
//   * 探测不进录制启动路径：Open 只做 id 的静态映射（LookupEncoder），不跑整表探测，
//     也不再与本文件的 MFStartup/MFShutdown 嵌套。
//   * 本产品编码器只吃 CPU NV12，探测器不创建/不持有 D3D device 或 video processor
//     （D-25 旧账无泄漏面）；COM/MF 资源用 ComPtr 与成对启停管理，失败可重试。
struct EncoderInfo {
    std::string id;          // 稳定标识：mf-h264-sw / mf-h264-hw / nvenc-h264 / qsv-h264 / amf-h264
    std::string name;        // 显示名
    bool hardware = false;   // 是否硬件编码
    bool available = false;  // 探测结论
    std::string reason;      // 不可用原因（available=true 时为空）
};

// 仅 SR_TEST_HOOKS 构建经 CLI 传入：让某一类 MFT 创建失败，
// 用于「人为让 mf-h264-hw 激活失败 → 列表必须变 unavailable」的反向用例（轮 47 指令 §P0-1）。
struct EncoderProbeOptions {
    bool fail_hardware = false;
    bool fail_software = false;
};

// 静态 id 查询：不启动 MF、不激活任何 MFT。录制启动路径用它把 id 映射成后端类型，
// 厂商项直接给「真实 dll 探测」得到的不可用原因；MF 项的实际可用性由后续
// CreateTransform / Negotiate / 码控链路判定（与列表探测共用同一份实现）。
struct EncoderLookup {
    bool known = false;
    bool available = false;  // false 时 reason 必非空（厂商项）
    bool hardware = false;
    std::string reason;
};

// 枚举 + 真激活探测。available=false 的项也在列表里，reason 说明原因。
std::vector<EncoderInfo> ListEncoders();
std::vector<EncoderInfo> ListEncoders(const EncoderProbeOptions& options);

// 按 id 做静态映射（不跑探测）。
EncoderLookup LookupEncoder(const std::string& id);

// 列表的规范单行序列化：recorder 的 stdout / 日志与 UI 的列表**共用这一份格式**，
// 保证「日志 encoders=[...] 与 UI 列表逐项一致」是构造上成立的，而不是靠两边对齐。
// 格式：id=xxx|state=available|id=yyy|state=unavailable|reason=zzz
std::string FormatEncoderList(const std::vector<EncoderInfo>& encoders);

// 按 id 找一项；找不到返回 nullptr。
const EncoderInfo* FindEncoder(const std::vector<EncoderInfo>& encoders, const std::string& id);

}  // namespace sr::encode
