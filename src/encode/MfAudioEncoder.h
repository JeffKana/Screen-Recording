#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/interfaces/IAudioEncoder.h"
#include "platform/ComPtr.h"

#include <mfobjects.h>
#include <mftransform.h>

namespace sr::encode {

using win::ComPtr;

// Media Foundation AAC 编码器（03 文档 §7）。
// 输入统一为 float32 交错，内部转 16bit PCM 交给 MFT；输出裸 AAC（payload type 0）。
class MfAudioEncoder final : public IAudioEncoder {
public:
    ~MfAudioEncoder() override;

    bool Open(const AudioEncoderConfig& config, std::string& error) override;
    bool Submit(const AudioBlock& block) override;
    bool TryReceive(EncodedPacket& out) override;
    bool Flush(std::string& error) override;
    void Close() override;

    const char* CodecName() const override { return "aac"; }

    // AudioSpecificConfig，MKV 的 CodecPrivate 用它。
    const std::vector<uint8_t>& CodecPrivate() const { return codec_private_; }
    bool IsOpen() const { return open_; }
    uint32_t EncodedFrames() const { return encoded_frames_; }

private:
    bool DrainOne(bool& produced, std::string& error);

    ComPtr<IMFTransform> transform_;
    std::vector<int16_t> pcm_;
    std::vector<uint8_t> packet_buffer_;
    std::vector<uint8_t> codec_private_;
    EncodedPacket packet_view_{};

    uint32_t sample_rate_ = 48000;
    uint16_t channels_ = 2;
    uint32_t bitrate_kbps_ = 192;
    int64_t submitted_frames_ = 0;
    uint32_t encoded_frames_ = 0;
    bool open_ = false;
    bool mf_started_ = false;
};

}  // namespace sr::encode
