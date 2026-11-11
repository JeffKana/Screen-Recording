#pragma once

#include <string>

#include "core/types/EncodingConfig.h"
#include "core/types/StorageConfig.h"

namespace sr {

// 封装抽象。实现：libavformat（LGPL 构建）。
// 契约：视频包与音频包必须按 DTS 单调写入；分片边界必须落在关键帧（02 文档 §4.1）。
class IMuxer {
public:
    virtual ~IMuxer() = default;

    virtual bool Open(const MuxerConfig& config, std::string& error) = 0;

    virtual bool WriteVideo(const EncodedPacket& packet) = 0;

    virtual bool WriteAudio(const EncodedPacket& packet) = 0;

    // 按 MuxerConfig 的分片条件判断是否需要切分；返回 true 表示已切出一个新分片。
    virtual bool RotateIfNeeded() = 0;

    // 收尾并把分片 remux 为成品。final_path 为实际产出路径（可能因重名而变更，M5-15）。
    virtual bool Finalize(std::string& final_path, std::string& error) = 0;

    // 异常收尾：保证已写入的分片结构完整，不做 remux（M8-01 恢复用）。
    virtual void Abort() = 0;

    virtual uint32_t SegmentCount() const = 0;

    virtual uint64_t BytesWritten() const = 0;
};

}  // namespace sr
