#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/interfaces/IVideoEncoder.h"
#include "platform/ComPtr.h"

#include <mfobjects.h>
#include <mftransform.h>

namespace sr::encode {

using win::ComPtr;

// Media Foundation H.264 编码器（03 文档 §6 的软编回退路径）。
//
// 厂商 SDK（NVENC / oneVPL / AMF）需要各自的头文件与二进制包，本环境不可得，
// 因此先用 MF 完成「硬编优先、软编回退」的骨架：MFTEnumEx 先查 HARDWARE MFT，
// 没有则退到 SYNCMFT（系统自带软编）。补上厂商 SDK 后，只需在本类里加一条分支。
class MfVideoEncoder final : public IVideoEncoder {
public:
    ~MfVideoEncoder() override;

    EncoderCaps Caps() const override;
    bool Open(const VideoEncoderConfig& config, std::string& error) override;

    // G3 阶段编码器只吃 CPU 内存的 NV12，故 Submit(GPU 纹理) 不支持。
    bool Submit(const VideoFrame& frame) override;

    // 提交一帧 CPU NV12。pts 为 100ns 单位（MF 原生时间基）。
    bool SubmitBytes(const uint8_t* data, size_t size, int64_t pts_100ns);

    bool TryReceive(EncodedPacket& out) override;
    bool Flush(std::string& error) override;
    void Close() override;
    bool DeviceLost() const override { return false; }

    // AVCDecoderConfigurationRecord（avcC），MKV 的 CodecPrivate 直接用它。
    const std::vector<uint8_t>& SequenceHeader() const { return sequence_header_; }
    bool Hardware() const { return hardware_; }
    const char* BackendName() const { return hardware_ ? "mf-h264-hw" : "mf-h264-sw"; }
    uint32_t EncodedFrames() const { return encoded_frames_; }
    uint32_t AnnexBConverted() const { return annexb_converted_; }
    uint64_t FillerBytes() const { return filler_bytes_; }
    uint32_t StreamChanges() const { return stream_changes_; }

    // M3-04：实际生效的码率控制。"cbr" / "vbr" = ICodecAPI 已确认生效；
    // "avg_bitrate" = 只有 MF_MT_AVG_BITRATE（编码器不支持码控或设置被拒）。
    const char* RateControlName() const { return rate_control_; }
    const std::string& RateControlError() const { return rate_control_error_; }
    uint32_t Quality() const { return quality_; }
    uint32_t MaxBitrateKbps() const { return max_bitrate_kbps_; }

    // M3-05：强制指定编码器（id 见 `sr-recorder --list-encoders`）。
    // 指定后**不做「硬编 → 软编」回退**：用户点名了，不可用/打不开就明确失败并给原因，
    // 走既有的 exit 6 路径（RecordingSession 的「encoder open failed」）。
    void SetRequestedId(const std::string& id) { requested_id_ = id; }
    const std::string& RequestedId() const { return requested_id_; }

private:
    bool CreateTransform(bool hardware, std::string& error);
    bool Negotiate(std::string& error);
    // 输出类型的码率/尺寸/帧率设置：协商与流变更后重设共用，避免退回默认码率。
    bool ConfigureOutputType(IMFMediaType* candidate);
    // 通过 ICodecAPI 设置 CBR / 峰值约束 VBR，并回读确认。失败时 rate_control_ 保持 avg_bitrate。
    bool ApplyRateControl(std::string& error);
    bool DrainOne(bool& produced, std::string& error);
    bool StorePacket(IMFSample* sample);
    // CBR 补齐：MF 软编的 CBR 只封顶不保底，静态桌面会远低于标称码率。
    // 按「每秒预算 - 本秒已出字节」追加 H.264 filler NAL（type 12），解码器会忽略。
    void PadToTargetBitrate();
    static bool AnnexBToAvcc(const uint8_t* in, size_t size, std::vector<uint8_t>& out);

    ComPtr<IMFTransform> transform_;
    std::vector<uint8_t> sequence_header_;
    std::vector<uint8_t> packet_buffer_;
    EncodedPacket packet_view_{};

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t fps_ = 30;
    uint32_t bitrate_kbps_ = 8000;
    uint32_t max_bitrate_kbps_ = 12000;
    uint32_t quality_ = 70;
    int64_t frame_duration_ = 0;
    int64_t submitted_ = 0;
    uint32_t encoded_frames_ = 0;
    uint32_t annexb_converted_ = 0;
    bool hardware_ = false;
    bool fail_all_encoders_ = false;
    bool open_ = false;
    bool mf_started_ = false;
    bool flushing_ = false;
    bool stream_change_handled_ = false;
    const char* rate_control_ = "avg_bitrate";
    std::string rate_control_error_;
    bool cbr_ = false;
    bool vbr_ = false;
    int64_t pad_window_ = -1;
    int64_t pad_bytes_in_window_ = 0;
    uint64_t filler_bytes_ = 0;
    uint32_t stream_changes_ = 0;
    std::string requested_id_;
};

}  // namespace sr::encode
