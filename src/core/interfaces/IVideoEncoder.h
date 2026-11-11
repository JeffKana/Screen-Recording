#pragma once

#include <string>

#include "core/types/EncodingConfig.h"
#include "core/types/Frame.h"

namespace sr {

// 视频编码抽象。实现：NVENC / oneVPL / AMF 硬编，MF H.264 软编 MFT 回退（03 文档 §6）。
class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;

    // 能力查询必须先于 Open 调用（M3-05）。
    virtual EncoderCaps Caps() const = 0;

    virtual bool Open(const VideoEncoderConfig& config, std::string& error) = 0;

    // 提交一帧。返回 false 表示编码器不可用，调用方须按 Fatal/Recoverable 处理。
    // 契约：本调用可以阻塞（编码慢时反压到帧队列），但不得在采集线程内调用。
    virtual bool Submit(const VideoFrame& frame) = 0;

    // 非阻塞收包。返回 false 表示暂无新包。
    virtual bool TryReceive(EncodedPacket& out) = 0;

    // 冲刷剩余帧（停止录制时调用），冲刷完成后必须保证所有包已可 TryReceive。
    virtual bool Flush(std::string& error) = 0;

    virtual void Close() = 0;

    // 运行期是否发生过设备丢失（TDR / 驱动重置），由上层触发重建（M8-09）。
    virtual bool DeviceLost() const = 0;
};

}  // namespace sr
