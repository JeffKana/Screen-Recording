#pragma once

#include <string>
#include <vector>

namespace sr::encode {

// M3-05：实际可用的视频编码器列表。
//
// 需求原文（docs/01）：「界面显示实际可用列表（如 NVENC / QSV / AMF / x264）」、
// 「不可用项不出现或标注不可用原因」。
//
// 口径（B 方案，轮 37 指令 §2）：
//   * 列表 = 产品**真的能选**的编码器 + 探测到不可用的厂商编码器（带真实原因）。
//     不可用项不消失——用户看不到「为什么没有硬编」就会去报 bug。
//   * 原因来自真实探测，不写死一句话：
//     - MF 的 H.264 MFT：MFTEnumEx 的枚举结果 + ActivateObject 是否成功；
//     - 厂商 SDK（NVENC / oneVPL / AMF）：驱动/运行库 dll 是否真实存在。
//   * 探测**不进录制启动路径**（轮 37 指令 §4.3）：只在设置页首次打开 /
//     `sr-recorder --list-encoders` 时调用。
struct EncoderInfo {
    std::string id;          // 稳定标识：mf-h264-sw / mf-h264-hw / nvenc-h264 / qsv-h264 / amf-h264
    std::string name;        // 显示名
    bool hardware = false;   // 是否硬件编码
    bool available = false;  // 探测结论
    std::string reason;      // 不可用原因（available=true 时为空）
};

// 枚举 + 探测。available=false 的项也在列表里，reason 说明原因。
std::vector<EncoderInfo> ListEncoders();

// 列表的规范单行序列化：recorder 的 stdout / 日志与 UI 的列表**共用这一份格式**，
// 保证「日志 encoders=[...] 与 UI 列表逐项一致」是构造上成立的，而不是靠两边对齐。
// 格式：id=xxx|state=available|id=yyy|state=unavailable|reason=zzz
std::string FormatEncoderList(const std::vector<EncoderInfo>& encoders);

// 按 id 找一项；找不到返回 nullptr。
const EncoderInfo* FindEncoder(const std::vector<EncoderInfo>& encoders, const std::string& id);

}  // namespace sr::encode
