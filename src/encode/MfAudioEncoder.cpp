#include "encode/MfAudioEncoder.h"

#include <algorithm>
#include <cstring>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>

namespace sr::encode {
namespace {

// AAC-LC 的 profile/level 指示值
constexpr uint32_t kAacProfileLc = 0x29;

}  // namespace

MfAudioEncoder::~MfAudioEncoder() {
    Close();
}

bool MfAudioEncoder::Open(const AudioEncoderConfig& config, std::string& error) {
    Close();

    sample_rate_ = config.sample_rate == 0 ? 48000 : config.sample_rate;
    channels_ = config.channels == 0 ? 2 : config.channels;
    bitrate_kbps_ = config.bitrate_kbps == 0 ? 192 : config.bitrate_kbps;

    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_owned = SUCCEEDED(com_hr);

    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        error = "MFStartup failed";
        if (com_owned) {
            CoUninitialize();
        }
        return false;
    }
    mf_started_ = true;

    MFT_REGISTER_TYPE_INFO in_info{MFMediaType_Audio, MFAudioFormat_PCM};
    MFT_REGISTER_TYPE_INFO out_info{MFMediaType_Audio, MFAudioFormat_AAC};

    IMFActivate** activates = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, MFT_ENUM_FLAG_SORTANDFILTER | MFT_ENUM_FLAG_SYNCMFT,
                           &in_info, &out_info, &activates, &count);
    if (FAILED(hr) || count == 0 || activates == nullptr) {
        error = "no AAC encoder MFT";
        Close();
        return false;
    }
    hr = activates[0]->ActivateObject(__uuidof(IMFTransform), reinterpret_cast<void**>(transform_.GetAddressOf()));
    for (UINT32 i = 0; i < count; ++i) {
        activates[i]->Release();
    }
    CoTaskMemFree(activates);
    if (FAILED(hr)) {
        error = "ActivateObject(AAC) failed";
        Close();
        return false;
    }

    // 音频编码器要求「先设输入、再设输出」，与视频编码器相反。
    ComPtr<IMFMediaType> input_type;
    if (FAILED(MFCreateMediaType(input_type.GetAddressOf()))) {
        error = "MFCreateMediaType failed";
        Close();
        return false;
    }
    const uint32_t block_align = channels_ * 2;
    input_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    input_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    input_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels_);
    input_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate_);
    input_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    input_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, block_align);
    input_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sample_rate_ * block_align);
    input_type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);

    hr = transform_->SetInputType(0, input_type.Get(), 0);
    if (FAILED(hr)) {
        error = "SetInputType(PCM16) failed";
        Close();
        return false;
    }

    // 输出类型：先试枚举到的可用类型，失败再退回自造类型。
    bool output_set = false;
    for (DWORD index = 0;; ++index) {
        ComPtr<IMFMediaType> candidate;
        const HRESULT enum_hr = transform_->GetOutputAvailableType(0, index, candidate.GetAddressOf());
        if (enum_hr == MF_E_NO_MORE_TYPES || FAILED(enum_hr)) {
            break;
        }
        GUID subtype{};
        if (FAILED(candidate->GetGUID(MF_MT_SUBTYPE, &subtype)) || subtype != MFAudioFormat_AAC) {
            continue;
        }
        candidate->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate_);
        candidate->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels_);
        candidate->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bitrate_kbps_ * 1000u / 8u);
        if (SUCCEEDED(transform_->SetOutputType(0, candidate.Get(), 0))) {
            output_set = true;
            break;
        }
    }

    if (!output_set) {
        ComPtr<IMFMediaType> output_type;
        if (FAILED(MFCreateMediaType(output_type.GetAddressOf()))) {
            error = "MFCreateMediaType failed";
            Close();
            return false;
        }
        output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        output_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        output_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate_);
        output_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels_);
        output_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bitrate_kbps_ * 1000u / 8u);
        output_type->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
        output_type->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, kAacProfileLc);
        hr = transform_->SetOutputType(0, output_type.Get(), 0);
        if (FAILED(hr)) {
            error = "SetOutputType(AAC) failed";
            Close();
            return false;
        }
    }
    // AudioSpecificConfig 在 MF_MT_USER_DATA 的末尾 2 字节。
    ComPtr<IMFMediaType> current;
    if (SUCCEEDED(transform_->GetOutputCurrentType(0, current.GetAddressOf())) && current) {
        UINT32 blob_size = 0;
        if (SUCCEEDED(current->GetBlobSize(MF_MT_USER_DATA, &blob_size)) && blob_size >= 2) {
            std::vector<uint8_t> blob(blob_size);
            if (SUCCEEDED(current->GetBlob(MF_MT_USER_DATA, blob.data(), blob_size, nullptr))) {
                codec_private_.assign(blob.end() - 2, blob.end());
            }
        }
    }

    transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    open_ = true;
    error.clear();
    return true;
}

bool MfAudioEncoder::Submit(const AudioBlock& block) {
    if (!open_ || transform_ == nullptr || block.samples == nullptr || block.desc.frame_count == 0) {
        return false;
    }
    if (block.desc.channels != channels_) {
        return false;
    }

    const uint32_t samples = block.desc.frame_count * channels_;
    pcm_.resize(samples);
    for (uint32_t i = 0; i < samples; ++i) {
        float value = block.samples[i];
        if (value > 1.0f) {
            value = 1.0f;
        } else if (value < -1.0f) {
            value = -1.0f;
        }
        pcm_[i] = static_cast<int16_t>(value * 32767.0f);
    }

    ComPtr<IMFSample> sample;
    if (FAILED(MFCreateSample(sample.GetAddressOf()))) {
        return false;
    }
    ComPtr<IMFMediaBuffer> buffer;
    const DWORD bytes = static_cast<DWORD>(samples * sizeof(int16_t));
    if (FAILED(MFCreateMemoryBuffer(bytes, buffer.GetAddressOf()))) {
        return false;
    }
    BYTE* destination = nullptr;
    if (FAILED(buffer->Lock(&destination, nullptr, nullptr))) {
        return false;
    }
    std::memcpy(destination, pcm_.data(), bytes);
    buffer->Unlock();
    buffer->SetCurrentLength(bytes);
    sample->AddBuffer(buffer.Get());

    // 时间戳按样本序号推导，保证与视频同一时间轴。
    const int64_t pts = block.desc.first_sample * 10000000LL / static_cast<int64_t>(sample_rate_);
    const int64_t duration = static_cast<int64_t>(block.desc.frame_count) * 10000000LL /
                             static_cast<int64_t>(sample_rate_);
    sample->SetSampleTime(pts);
    sample->SetSampleDuration(duration);

    HRESULT hr = transform_->ProcessInput(0, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING) {
        bool produced = false;
        std::string drain_error;
        while (DrainOne(produced, drain_error) && produced) {
        }
        hr = transform_->ProcessInput(0, sample.Get(), 0);
    }
    if (FAILED(hr)) {
        return false;
    }
    submitted_frames_ += block.desc.frame_count;
    return true;
}

bool MfAudioEncoder::DrainOne(bool& produced, std::string& error) {
    produced = false;
    if (transform_ == nullptr) {
        error = "encoder is not open";
        return false;
    }

    MFT_OUTPUT_STREAM_INFO stream_info{};
    if (FAILED(transform_->GetOutputStreamInfo(0, &stream_info))) {
        error = "GetOutputStreamInfo failed";
        return false;
    }

    MFT_OUTPUT_DATA_BUFFER output{};
    ComPtr<IMFSample> owned;
    if ((stream_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0) {
        if (FAILED(MFCreateSample(owned.GetAddressOf()))) {
            error = "MFCreateSample failed";
            return false;
        }
        ComPtr<IMFMediaBuffer> buffer;
        const DWORD capacity = stream_info.cbSize != 0 ? stream_info.cbSize : (32u * 1024u);
        if (FAILED(MFCreateMemoryBuffer(capacity, buffer.GetAddressOf()))) {
            error = "MFCreateMemoryBuffer failed";
            return false;
        }
        owned->AddBuffer(buffer.Get());
        output.pSample = owned.Get();
    }

    DWORD status = 0;
    const HRESULT hr = transform_->ProcessOutput(0, 1, &output, &status);
    if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
        ComPtr<IMFMediaType> new_type;
        if (SUCCEEDED(transform_->GetOutputAvailableType(0, 0, new_type.GetAddressOf()))) {
            transform_->SetOutputType(0, new_type.Get(), 0);
        }
        return true;
    }
    if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
        return true;
    }
    if (FAILED(hr)) {
        error = "ProcessOutput failed";
        return false;
    }

    if (output.pSample != nullptr) {
        ComPtr<IMFMediaBuffer> buffer;
        if (SUCCEEDED(output.pSample->ConvertToContiguousBuffer(buffer.GetAddressOf()))) {
            BYTE* data = nullptr;
            DWORD length = 0;
            if (SUCCEEDED(buffer->Lock(&data, nullptr, &length)) && length > 0) {
                packet_buffer_.assign(data, data + length);
                LONGLONG time = 0;
                output.pSample->GetSampleTime(&time);
                packet_view_.data = packet_buffer_.data();
                packet_view_.size = packet_buffer_.size();
                packet_view_.pts = Tick{time};
                packet_view_.dts = Tick{time};
                packet_view_.is_audio = true;
                packet_view_.keyframe = true;
                ++encoded_frames_;
                produced = true;
            }
            buffer->Unlock();
        }
        if (owned == nullptr) {
            output.pSample->Release();
        }
    }
    if (output.pEvents != nullptr) {
        output.pEvents->Release();
    }
    return true;
}

bool MfAudioEncoder::TryReceive(EncodedPacket& out) {
    bool produced = false;
    std::string error;
    if (!DrainOne(produced, error) || !produced) {
        return false;
    }
    out = packet_view_;
    return true;
}

bool MfAudioEncoder::Flush(std::string& error) {
    if (!open_ || transform_ == nullptr) {
        error = "encoder is not open";
        return false;
    }
    transform_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
    error.clear();
    return true;
}

void MfAudioEncoder::Close() {
    if (transform_ != nullptr) {
        transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    }
    transform_.Reset();
    if (mf_started_) {
        MFShutdown();
        mf_started_ = false;
    }
    open_ = false;
    submitted_frames_ = 0;
    encoded_frames_ = 0;
    pcm_.clear();
}

}  // namespace sr::encode
