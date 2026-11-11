#include "audio/WasapiSource.h"

#include <cmath>
#include <cstring>

namespace sr::audio {
namespace {

constexpr REFERENCE_TIME kBufferDuration = 200000;  // 20ms，单位 100ns

bool IsFloatFormat(const WAVEFORMATEX& format) {
    if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return true;
    }
    // 共享模式下混音格式几乎都是 EXTENSIBLE + 32bit float。
    // 这里不做 SubFormat GUID 判断，避免引入 ksmedia.h 的平台差异。
    return format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && format.wBitsPerSample == 32;
}

}  // namespace

WasapiSource::WasapiSource(const IClock& clock, AudioRole role) : clock_(clock), role_(role) {}

WasapiSource::~WasapiSource() {
    Stop();
}

bool WasapiSource::Start(std::string& error) {
    Stop();

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        com_initialized_ = true;
    } else if (hr != RPC_E_CHANGED_MODE) {
        error = "CoInitializeEx failed";
        return false;
    }

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator_.GetAddressOf()));
    if (FAILED(hr)) {
        error = "MMDeviceEnumerator creation failed";
        Stop();
        return false;
    }

    const EDataFlow flow = (role_ == AudioRole::SystemLoopback) ? eRender : eCapture;
    hr = enumerator_->GetDefaultAudioEndpoint(flow, eConsole, device_.GetAddressOf());
    if (FAILED(hr)) {
        error = (role_ == AudioRole::SystemLoopback) ? "no default playback device"
                                                    : "no default recording device";
        Stop();
        return false;
    }

    LPWSTR id = nullptr;
    if (SUCCEEDED(device_->GetId(&id)) && id != nullptr) {
        device_id_ = id;
        CoTaskMemFree(id);
    }

    IPropertyStore* store = nullptr;
    if (SUCCEEDED(device_->OpenPropertyStore(STGM_READ, &store)) && store != nullptr) {
        PROPVARIANT value{};
        PropVariantInit(&value);
        // PKEY_Device_FriendlyName 的固定 GUID/PID，避免依赖 functiondiscoverykeys_devpkey.h。
        const PROPERTYKEY friendly_name = {
            {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
        if (SUCCEEDED(store->GetValue(friendly_name, &value)) && value.vt == VT_LPWSTR && value.pwszVal) {
            device_name_ = value.pwszVal;
        }
        PropVariantClear(&value);
        store->Release();
    }

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(client_.GetAddressOf()));
    if (FAILED(hr)) {
        error = "IAudioClient activation failed";
        Stop();
        return false;
    }

    hr = client_->GetMixFormat(&mix_format_);
    if (FAILED(hr) || mix_format_ == nullptr) {
        error = "GetMixFormat failed";
        Stop();
        return false;
    }
    sample_rate_ = mix_format_->nSamplesPerSec;
    channels_ = mix_format_->nChannels;
    is_float_ = IsFloatFormat(*mix_format_);

    DWORD flags = 0;
    if (role_ == AudioRole::SystemLoopback) {
        flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    }

    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, kBufferDuration, 0, mix_format_, nullptr);
    if (FAILED(hr)) {
        error = "IAudioClient::Initialize failed";
        Stop();
        return false;
    }

    hr = client_->GetService(__uuidof(IAudioCaptureClient),
                             reinterpret_cast<void**>(capture_.GetAddressOf()));
    if (FAILED(hr)) {
        error = "IAudioCaptureClient unavailable";
        Stop();
        return false;
    }

    hr = client_->Start();
    if (FAILED(hr)) {
        error = "IAudioClient::Start failed";
        Stop();
        return false;
    }

    needs_rebuild_ = false;
    produced_frames_ = 0;
    error.clear();
    return true;
}

void WasapiSource::Stop() {
    if (client_ != nullptr) {
        client_->Stop();
    }
    capture_.Reset();
    client_.Reset();
    device_.Reset();
    enumerator_.Reset();
    if (mix_format_ != nullptr) {
        CoTaskMemFree(mix_format_);
        mix_format_ = nullptr;
    }
    buffer_.clear();
    if (com_initialized_) {
        CoUninitialize();
        com_initialized_ = false;
    }
}

void WasapiSource::CheckDeviceChange() {
    if (enumerator_ == nullptr || needs_rebuild_) {
        return;
    }
    // 每 100 次轮询（约 1 秒）查一次，避免高频 COM 调用。
    if (++poll_counter_ % 100 != 0) {
        return;
    }
    ComPtr<IMMDevice> current;
    const EDataFlow flow = (role_ == AudioRole::SystemLoopback) ? eRender : eCapture;
    if (FAILED(enumerator_->GetDefaultAudioEndpoint(flow, eConsole, current.GetAddressOf()))) {
        needs_rebuild_ = true;
        return;
    }
    LPWSTR id = nullptr;
    if (SUCCEEDED(current->GetId(&id)) && id != nullptr) {
        if (device_id_ != id) {
            needs_rebuild_ = true;
        }
        CoTaskMemFree(id);
    }
}

void WasapiSource::AppendPacket(const BYTE* data, UINT32 frames, bool silent) {
    const size_t samples = static_cast<size_t>(frames) * channels_;
    const size_t base = buffer_.size();
    buffer_.resize(base + samples);

    if (silent || data == nullptr) {
        std::memset(buffer_.data() + base, 0, samples * sizeof(float));
        return;
    }

    float block_peak = 0.0f;
    if (is_float_) {
        std::memcpy(buffer_.data() + base, data, samples * sizeof(float));
        for (size_t i = 0; i < samples; ++i) {
            const float magnitude = std::fabs(buffer_[base + i]);
            if (magnitude > block_peak) {
                block_peak = magnitude;
            }
        }
    } else {
        const auto* pcm = reinterpret_cast<const int16_t*>(data);
        for (size_t i = 0; i < samples; ++i) {
            const float value = static_cast<float>(pcm[i]) / 32768.0f;
            buffer_[base + i] = value;
            const float magnitude = std::fabs(value);
            if (magnitude > block_peak) {
                block_peak = magnitude;
            }
        }
    }
    peak_ = block_peak;
}

bool WasapiSource::TryRead(AudioBlock& out) {
    if (capture_ == nullptr) {
        return false;
    }

    CheckDeviceChange();
    buffer_.clear();

    UINT32 packet_frames = 0;
    while (SUCCEEDED(capture_->GetNextPacketSize(&packet_frames)) && packet_frames > 0) {
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        if (FAILED(capture_->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
            break;
        }
        AppendPacket(data, frames, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);
        capture_->ReleaseBuffer(frames);
    }

    if (buffer_.empty()) {
        return false;  // 静音期的 Loopback 就是走到这里
    }

    const uint32_t frame_count = static_cast<uint32_t>(buffer_.size() / channels_);
    out.samples = buffer_.data();
    out.desc.first_sample = static_cast<int64_t>(produced_frames_);
    out.desc.sample_rate = sample_rate_;
    out.desc.channels = channels_;
    out.desc.frame_count = frame_count;
    produced_frames_ += frame_count;
    return true;
}

}  // namespace sr::audio
