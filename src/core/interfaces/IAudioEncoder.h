#pragma once

#include <string>

#include "core/types/EncodingConfig.h"
#include "core/types/Frame.h"

namespace sr {

// 音频编码抽象。实现：Media Foundation AAC MFT（03 文档 §7）。
class IAudioEncoder {
public:
    virtual ~IAudioEncoder() = default;

    virtual bool Open(const AudioEncoderConfig& config, std::string& error) = 0;

    virtual bool Submit(const AudioBlock& block) = 0;

    virtual bool TryReceive(EncodedPacket& out) = 0;

    virtual bool Flush(std::string& error) = 0;

    virtual void Close() = 0;

    virtual const char* CodecName() const = 0;
};

}  // namespace sr
