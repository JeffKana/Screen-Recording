#pragma once

#include <string>
#include <vector>

#include "core/interfaces/IAudioSource.h"
#include "core/types/IClock.h"
#include "platform/ComPtr.h"

#include <audioclient.h>
#include <mmdeviceapi.h>

namespace sr::audio {

using win::ComPtr;

enum class AudioRole {
    SystemLoopback = 0,  // eRender + LOOPBACK（M1-15）
    Microphone,          // eCapture（M1-17）
};

// WASAPI 音频源。共享模式、事件/轮询混合驱动，禁止独占（会抢走设备给别的程序）。
class WasapiSource final : public IAudioSource {
public:
    WasapiSource(const IClock& clock, AudioRole role);
    ~WasapiSource() override;

    bool Start(std::string& error) override;
    void Stop() override;
    bool IsRunning() const override { return client_ != nullptr; }
    bool TryRead(AudioBlock& out) override;

    // 回环源在系统静音期完全不产出数据包 —— 这正是 M1-21 静音填充的触发依据。
    bool MayStarveWhenSilent() const override { return role_ == AudioRole::SystemLoopback; }

    bool NeedsRebuild() const override { return needs_rebuild_; }

    // M1-17 的电平指示：最近一块的峰值（0.0 ~ 1.0+）。
    float PeakLevel() const { return peak_; }

    const wchar_t* DeviceName() const { return device_name_.c_str(); }
    uint32_t SampleRate() const { return sample_rate_; }
    uint16_t Channels() const { return channels_; }

private:
    void AppendPacket(const BYTE* data, UINT32 frames, bool silent);
    void CheckDeviceChange();

    const IClock& clock_;
    AudioRole role_;

    ComPtr<IMMDeviceEnumerator> enumerator_;
    ComPtr<IMMDevice> device_;
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioCaptureClient> capture_;

    WAVEFORMATEX* mix_format_ = nullptr;
    std::wstring device_id_;
    std::wstring device_name_;

    std::vector<float> buffer_;
    uint32_t sample_rate_ = 0;
    uint16_t channels_ = 0;
    bool is_float_ = true;
    bool com_initialized_ = false;

    uint64_t produced_frames_ = 0;
    float peak_ = 0.0f;
    uint32_t poll_counter_ = 0;
    bool needs_rebuild_ = false;
};

}  // namespace sr::audio
