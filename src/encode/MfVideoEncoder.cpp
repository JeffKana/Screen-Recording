#include "encode/MfVideoEncoder.h"

#include "encode/EncoderList.h"

#include <cstring>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <oleauto.h>
// ICodecAPI 声明在 strmif.h（DirectShow）。只用到接口定义，不链接 strmiids：
// 属性 GUID 在下面按 codecapi.h 的原值本地定义，避免给两个工具链各加一个库依赖。
#include <strmif.h>

namespace sr::encode {
namespace {

// 取自 SDK 的 um/codecapi.h（值逐字节一致；本地定义以避开 strmiids.lib）。
constexpr GUID kCodecApiICodecApi = {
    0x901db4c7, 0x31ce, 0x41a2, {0x85, 0xdc, 0x8f, 0xa0, 0xbf, 0x41, 0xb8, 0xda}};
constexpr GUID kCodecApiRateControlMode = {
    0x1c0608e9, 0x370c, 0x4710, {0x8a, 0x58, 0xcb, 0x61, 0x81, 0xc4, 0x24, 0x23}};
constexpr GUID kCodecApiMeanBitRate = {
    0xf7222374, 0x2144, 0x4815, {0xb5, 0x50, 0xa3, 0x7f, 0x8e, 0x12, 0xee, 0x52}};
constexpr GUID kCodecApiMaxBitRate = {
    0x9651eae4, 0x39b9, 0x4ebf, {0x85, 0xef, 0xd7, 0xf4, 0x44, 0xec, 0x74, 0x65}};
constexpr GUID kCodecApiBufferSize = {
    0x0db96574, 0xb6a4, 0x4c8b, {0x81, 0x06, 0x37, 0x73, 0xde, 0x03, 0x10, 0xcd}};
constexpr GUID kCodecApiQuality = {
    0xfcbf57a3, 0x7ea5, 0x4b0c, {0x96, 0x44, 0x69, 0xb4, 0x0c, 0x39, 0xc3, 0x91}};
constexpr uint32_t kRateControlCbr = 0;  // eAVEncCommonRateControlMode_CBR
// M3-04#2：平均码率 + 峰值上限 = PeakConstrainedVBR（codecapi.h 枚举值 1）。
constexpr uint32_t kRateControlPeakConstrainedVbr = 1;
// CBR 下的画质上限（0~100）。不设时编码器会对关键帧用近无损质量，单秒冲到标称的 2 倍。
constexpr uint32_t kCbrQualityCap = 70;
// VBR 默认画质档位：比 CBR 上限高一档，否则「VBR」只会变成低码率劣化模式。
constexpr uint32_t kDefaultVbrQuality = 80;

bool SetCodecUint32(ICodecAPI* api, const GUID& id, uint32_t value) {
    VARIANT var;
    VariantInit(&var);
    var.vt = VT_UI4;
    var.ulVal = value;
    const HRESULT hr = api->SetValue(&id, &var);
    VariantClear(&var);
    return SUCCEEDED(hr);
}

bool GetCodecUint32(ICodecAPI* api, const GUID& id, uint32_t& value) {
    VARIANT var;
    VariantInit(&var);
    const HRESULT hr = api->GetValue(&id, &var);
    bool ok = false;
    if (SUCCEEDED(hr) && var.vt == VT_UI4) {
        value = var.ulVal;
        ok = true;
    }
    VariantClear(&var);
    return ok;
}

// MF 的 H.264 编码器通常给 Annex-B 形式的参数集（00 00 00 01 SPS ... PPS）。
// MKV 的 CodecPrivate 必须是 AVCDecoderConfigurationRecord，这里做一次转换。
int64_t HundredNs(uint32_t fps) {
    return fps == 0 ? 333333 : (10000000LL / static_cast<int64_t>(fps));
}

// MF 的 H.264 编码器通常给 Annex-B 形式的参数集，MKV 的 CodecPrivate 必须是 avcC。
bool AnnexBToAvcC(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    // 起始码可能是 3 字节（00 00 01）也可能是 4 字节（00 00 00 01），两种都要认。
    std::vector<size_t> nal_starts;
    std::vector<size_t> nal_ends;
    for (size_t i = 0; i + 3 <= in.size();) {
        if (in[i] == 0 && in[i + 1] == 0 && in[i + 2] == 1) {
            if (!nal_starts.empty()) {
                size_t end = i;
                // 4 字节起始码前面的 0 属于下一个起始码，不属于上一个 NAL。
                if (end > nal_starts.back() && in[end - 1] == 0) {
                    --end;
                }
                nal_ends.push_back(end);
            }
            nal_starts.push_back(i + 3);
            i += 3;
        } else {
            ++i;
        }
    }
    if (nal_starts.empty()) {
        return false;
    }
    nal_ends.push_back(in.size());

    const uint8_t* sps = nullptr;
    size_t sps_size = 0;
    const uint8_t* pps = nullptr;
    size_t pps_size = 0;
    for (size_t i = 0; i < nal_starts.size(); ++i) {
        const size_t start = nal_starts[i];
        const size_t end = nal_ends[i];
        if (end <= start) {
            continue;
        }
        const uint8_t nal_type = in[start] & 0x1F;
        if (nal_type == 7 && sps == nullptr) {
            sps = in.data() + start;
            sps_size = end - start;
        } else if (nal_type == 8 && pps == nullptr) {
            pps = in.data() + start;
            pps_size = end - start;
        }
    }
    if (sps == nullptr || sps_size < 4 || pps == nullptr || pps_size == 0) {
        return false;
    }

    out.clear();
    out.reserve(11 + sps_size + pps_size);
    out.push_back(0x01);
    out.push_back(sps[1]);
    out.push_back(sps[2]);
    out.push_back(sps[3]);
    out.push_back(0xFF);  // lengthSizeMinusOne = 3（4 字节长度前缀）
    out.push_back(0xE1);  // numOfSequenceParameterSets = 1
    out.push_back(static_cast<uint8_t>((sps_size >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(sps_size & 0xFF));
    out.insert(out.end(), sps, sps + sps_size);
    out.push_back(0x01);  // numOfPictureParameterSets = 1
    out.push_back(static_cast<uint8_t>((pps_size >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(pps_size & 0xFF));
    out.insert(out.end(), pps, pps + pps_size);
    return true;
}
}  // namespace

MfVideoEncoder::~MfVideoEncoder() {
    Close();
}

EncoderCaps MfVideoEncoder::Caps() const {
    EncoderCaps caps;
    caps.name = BackendName();
    caps.hardware = hardware_;
    caps.max_width = 3840;
    caps.max_height = 2160;
    caps.max_fps = 60;
    caps.max_bitrate_kbps = 50000;
    caps.supports_hevc = false;
    caps.supports_av1 = false;
    caps.supports_10bit = false;
    caps.accepts_gpu_texture = false;  // G3 走 CPU NV12
    return caps;
}

bool MfVideoEncoder::CreateTransform(bool hardware, std::string& error) {
    // 注入落在真实的枚举/创建函数内部，Open 里的硬编→软编→失败三条分支都要真实走一遍。
    // 轮 47 新增定向注入（--fail-encoder hardware|software）：无参形式仍等于「全部失败」。
    if (fail_all_encoders_ || (hardware && fail_hardware_encoder_) ||
        (!hardware && fail_software_encoder_)) {
        error = hardware ? "injected: hardware encoder unavailable"
                         : "injected: software encoder unavailable";
        return false;
    }
    MFT_REGISTER_TYPE_INFO in_info{MFMediaType_Video, MFVideoFormat_NV12};
    MFT_REGISTER_TYPE_INFO out_info{MFMediaType_Video, MFVideoFormat_H264};
    const UINT32 flags = MFT_ENUM_FLAG_SORTANDFILTER |
                         (hardware ? MFT_ENUM_FLAG_HARDWARE : MFT_ENUM_FLAG_SYNCMFT);

    IMFActivate** activates = nullptr;
    UINT32 count = 0;
    const HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, flags, &in_info, &out_info, &activates, &count);
    if (FAILED(hr) || count == 0 || activates == nullptr) {
        error = hardware ? "no hardware H.264 MFT" : "no software H.264 MFT";
        return false;
    }

    const HRESULT activate_hr =
        activates[0]->ActivateObject(__uuidof(IMFTransform), reinterpret_cast<void**>(transform_.GetAddressOf()));
    for (UINT32 i = 0; i < count; ++i) {
        activates[i]->Release();
    }
    CoTaskMemFree(activates);

    if (FAILED(activate_hr)) {
        error = "ActivateObject failed";
        return false;
    }
    hardware_ = hardware;
    return true;
}

bool MfVideoEncoder::Negotiate(std::string& error) {
    // 输出类型：枚举编码器自带的可选类型，挑 H.264 并补齐尺寸/帧率/码率。
    // 直接 MFCreateMediaType 自造类型在某些 MFT 上会被拒（缺少厂商要求的属性）。
    bool output_set = false;
    for (DWORD index = 0;; ++index) {
        ComPtr<IMFMediaType> candidate;
        const HRESULT enum_hr = transform_->GetOutputAvailableType(0, index, candidate.GetAddressOf());
        if (enum_hr == MF_E_NO_MORE_TYPES || FAILED(enum_hr)) {
            break;
        }
        GUID subtype{};
        if (FAILED(candidate->GetGUID(MF_MT_SUBTYPE, &subtype)) || subtype != MFVideoFormat_H264) {
            continue;
        }
        if (ConfigureOutputType(candidate.Get())) {
            output_set = true;
            break;
        }
    }
    // 有些编码器在输入类型确定前不枚举输出类型，这里先不判死，等输入就位后重试。

    bool input_set = false;
    for (DWORD index = 0;; ++index) {
        ComPtr<IMFMediaType> candidate;
        const HRESULT enum_hr = transform_->GetInputAvailableType(0, index, candidate.GetAddressOf());
        if (enum_hr == MF_E_NO_MORE_TYPES || FAILED(enum_hr)) {
            break;
        }
        GUID subtype{};
        if (FAILED(candidate->GetGUID(MF_MT_SUBTYPE, &subtype)) || subtype != MFVideoFormat_NV12) {
            continue;
        }
        candidate->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(candidate.Get(), MF_MT_FRAME_SIZE, width_, height_);
        MFSetAttributeRatio(candidate.Get(), MF_MT_FRAME_RATE, fps_, 1);
        MFSetAttributeRatio(candidate.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (SUCCEEDED(transform_->SetInputType(0, candidate.Get(), 0))) {
            input_set = true;
            break;
        }
    }
    if (!input_set) {
        error = "encoder rejected NV12 input type";
        return false;
    }
    // 输入类型就位后再试一次输出类型（应对「必须先设输入」的编码器）。
    if (!output_set) {
        for (DWORD index = 0;; ++index) {
            ComPtr<IMFMediaType> candidate;
            const HRESULT enum_hr = transform_->GetOutputAvailableType(0, index, candidate.GetAddressOf());
            if (enum_hr == MF_E_NO_MORE_TYPES || FAILED(enum_hr)) {
                break;
            }
            GUID subtype{};
            if (FAILED(candidate->GetGUID(MF_MT_SUBTYPE, &subtype)) || subtype != MFVideoFormat_H264) {
                continue;
            }
            if (ConfigureOutputType(candidate.Get())) {
                output_set = true;
                break;
            }
        }
    }
    if (!output_set) {
        error = "cannot negotiate H.264 output type";
        return false;
    }

    // 码率控制（CBR）在 Open 里通过 ICodecAPI 单独设置（见 ApplyRateControl）：
    // 输出类型上的 MF_MT_AVG_BITRATE 只是「目标均值」，MF 软编会按场景内容大幅低于它。
    // 从协商后的输出类型取 avcC。取不到也不致命：有些编码器把 SPS/PPS 嵌在首帧里。
    ComPtr<IMFMediaType> current;
    if (SUCCEEDED(transform_->GetOutputCurrentType(0, current.GetAddressOf())) && current) {
        UINT32 blob_size = 0;
        if (SUCCEEDED(current->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &blob_size)) && blob_size > 0) {
            sequence_header_.resize(blob_size);
            if (FAILED(current->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER,
                                        sequence_header_.data(),
                                        blob_size,
                                        nullptr))) {
                sequence_header_.clear();
            } else {
                // MF 给的是 Annex-B 参数集，MKV 的 CodecPrivate 必须是 avcC。
                std::vector<uint8_t> avcc;
                if (AnnexBToAvcC(sequence_header_, avcc)) {
                    sequence_header_ = std::move(avcc);
                }
            }
        }
    }

    return true;
}

bool MfVideoEncoder::ConfigureOutputType(IMFMediaType* candidate) {
    // 输出类型的公共设置。协商、以及流变更后的重设都必须走这里，
    // 否则重设会用编码器默认码率——实测表现为「某几秒突然冲到标称的 2 倍」。
    candidate->SetUINT32(MF_MT_AVG_BITRATE, bitrate_kbps_ * 1000u);
    candidate->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(candidate, MF_MT_FRAME_SIZE, width_, height_);
    MFSetAttributeRatio(candidate, MF_MT_FRAME_RATE, fps_, 1);
    MFSetAttributeRatio(candidate, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    return SUCCEEDED(transform_->SetOutputType(0, candidate, 0));
}

bool MfVideoEncoder::ApplyRateControl(std::string& error) {
    rate_control_ = "avg_bitrate";
    rate_control_error_.clear();
    if (transform_ == nullptr) {
        error = "encoder is not open";
        return false;
    }
    ComPtr<ICodecAPI> api;
    const HRESULT queried =
        transform_->QueryInterface(kCodecApiICodecApi, reinterpret_cast<void**>(api.GetAddressOf()));
    if (FAILED(queried) || api == nullptr) {
        error = "ICodecAPI not exposed";
        return false;
    }
    // 先给均值码率、再切模式：切模式时已有目标码率，避免编码器按默认值起步。
    if (!SetCodecUint32(api.Get(), kCodecApiMeanBitRate, bitrate_kbps_ * 1000u)) {
        error = "CODECAPI_AVEncCommonMeanBitRate rejected";
        return false;
    }
    const uint32_t target_mode = cbr_ ? kRateControlCbr : kRateControlPeakConstrainedVbr;
    const uint32_t target_max = cbr_ ? bitrate_kbps_ * 1000u : max_bitrate_kbps_ * 1000u;
    // 逐秒恒定靠三件事，缺一条都会实测超标（6000kbps 档的历史值）：
    //   1) mean=max=标称、buffer 取 0.1s 额度：0.5s 窗口时单秒可冲到 11,118kbps（spread 82%）；
    //   2) 画质上限 70：不限画质时，关键帧/场景切换会用近无损质量，单秒冲到 10,637kbps
    //      （此时 buffer 与 target 怎么调都无效——实测 target 40% 反而冲到 7,990kbps）；
    //   3) filler 补齐（见 PadToTargetBitrate）：MF 的 CBR 只封顶不保底。
    // VBR 只需要 1) 的均值与峰值上限 + 2) 的画质档位，不做 filler（否则就不是 VBR 了）。
    const bool max_ok = SetCodecUint32(api.Get(), kCodecApiMaxBitRate, target_max);
    SetCodecUint32(api.Get(), kCodecApiBufferSize,
                   cbr_ ? bitrate_kbps_ * 100u : max_bitrate_kbps_ * 100u);
    const bool quality_ok = SetCodecUint32(api.Get(), kCodecApiQuality, quality_);
    if (!cbr_) {
        // 峰值上限是 VBR 的硬判据：max 设置被拒时不能谎报 vbr。
        // 画质档位只做尽力而为：实测 MF 软编在 PeakConstrainedVBR 下会拒绝
        // CODECAPI_AVEncCommonQuality（CBR 下接受），不能因此判整个 VBR 失败。
        if (!max_ok) {
            error = "CODECAPI VBR max bitrate rejected";
            return false;
        }
        (void)quality_ok;
    }
    if (!SetCodecUint32(api.Get(), kCodecApiRateControlMode, target_mode)) {
        error = cbr_ ? "CODECAPI_AVEncCommonRateControlMode=CBR rejected"
                     : "CODECAPI_AVEncCommonRateControlMode=PeakConstrainedVBR rejected";
        return false;
    }
    // 回读确认：有的 MFT 接受 SetValue 却不生效，只认回读值。
    uint32_t mode = 0xFFFFFFFF;
    if (!GetCodecUint32(api.Get(), kCodecApiRateControlMode, mode) || mode != target_mode) {
        error = "rate control read-back mismatch";
        return false;
    }
    // 回读峰值上限：只回读「模式」不够，实测有 MFT 接受 max 却回落到默认值。
    uint32_t read_back_max = 0;
    if (GetCodecUint32(api.Get(), kCodecApiMaxBitRate, read_back_max) && read_back_max != 0 &&
        read_back_max != target_max) {
        error = "max bitrate read-back mismatch";
        return false;
    }
    // 如实上报实际生效的画质档位：该 MFT 可能接受写入但回读为默认值。
    // 轮 34 R3 实测：CBR 下 70/85 回读均成功，但成片 PSNR/SSIM 完全一致（33.78dB / 0.9935），
    // 该档位无可测效果；CBR 稳定化来自 mean=max + 0.1s buffer + filler，不靠 quality。
    uint32_t quality_readback = 0;
    if (GetCodecUint32(api.Get(), kCodecApiQuality, quality_readback)) {
        quality_ = quality_readback;
    }
    rate_control_ = cbr_ ? "cbr" : "vbr";
    return true;
}

bool MfVideoEncoder::Open(const VideoEncoderConfig& config, std::string& error) {
    Close();

    width_ = config.width & ~1u;
    height_ = config.height & ~1u;
    fps_ = config.fps == 0 ? 30 : config.fps;
    bitrate_kbps_ = config.bitrate_kbps == 0 ? 8000 : config.bitrate_kbps;
    max_bitrate_kbps_ = config.max_bitrate_kbps == 0 ? bitrate_kbps_ * 2 : config.max_bitrate_kbps;
    if (max_bitrate_kbps_ < bitrate_kbps_) {
        max_bitrate_kbps_ = bitrate_kbps_;  // 上限低于均值是无效组合
    }
    // M3-04：CBR / VBR 都接 ICodecAPI。0 表示按模式取默认档位；只对 CBR 做 filler 补齐。
    cbr_ = config.rate_control == RateControlMode::Cbr;
    vbr_ = config.rate_control == RateControlMode::Vbr;
    quality_ = config.quality == 0 ? (cbr_ ? kCbrQualityCap : kDefaultVbrQuality)
                                   : (config.quality > 100 ? 100 : config.quality);
    frame_duration_ = HundredNs(fps_);
    filler_bytes_ = 0;
    stream_changes_ = 0;
    pad_window_ = -1;
    pad_bytes_in_window_ = 0;

    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    com_owned_ = SUCCEEDED(com_hr);

    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        error = "MFStartup failed";
        if (com_owned_) {
            CoUninitialize();
            com_owned_ = false;
        }
        return false;
    }
    mf_started_ = true;

    std::string hardware_error;
    fail_all_encoders_ = config.fail_all_encoders;
    const bool forced = !requested_id_.empty();
    bool created = false;
    if (forced) {
        // M3-05：显式指定编码器 → 只用这一个，且不回退。
        // 轮 47：不再在录制启动路径跑整表真激活探测（会与 MF 启停嵌套、也重复劳动）。
        // id 只做静态映射；真正的「能不能用」由下面同一个创建/协商/码控链路判定，
        // 任何失败统一带 "unavailable: <原因>" 走 exit 6，与列表的探测结论同源。
        const sr::encode::EncoderLookup lookup = sr::encode::LookupEncoder(requested_id_);
        if (!lookup.known) {
            error = "encoder '" + requested_id_ + "' not found (see sr-recorder --list-encoders)";
            Close();
            return false;
        }
        if (!lookup.available) {
            error = "encoder '" + requested_id_ + "' unavailable: " + lookup.reason;
            Close();
            return false;
        }
        created = CreateTransform(lookup.hardware, error);
        if (!created) {
            error = "encoder '" + requested_id_ + "' unavailable: " + error;
            Close();
            return false;
        }
    } else {
        created = CreateTransform(true, hardware_error);
        if (!created) {
            created = CreateTransform(false, error);
            if (!created) {
                error += " (" + hardware_error + ")";
                Close();
                return false;
            }
        }
    }

    // 硬编能创建不等于能协商：自动选择时协商失败回退到软编（M3-06 的确定性回退）。
    // 轮 47 F1：被点名的编码器**不许回退**——静默换成软编会让用户以为自己在用硬编。
    if (!Negotiate(error)) {
        if (hardware_ && !forced) {
            const std::string hardware_negotiate_error = error;
            transform_.Reset();
            std::string software_error;
            if (!CreateTransform(false, software_error) || !Negotiate(software_error)) {
                error = "hardware: " + hardware_negotiate_error + "; software: " + software_error;
                Close();
                return false;
            }
        } else {
            if (forced) {
                error = "encoder '" + requested_id_ + "' unavailable: MFT 协商失败（" + error + "）";
            }
            Close();
            return false;
        }
    }

    // M3-04：CBR/VBR 都依赖 ICodecAPI。硬编不接受码控时按 M3-06 的方向先试软编；
    // 软编也不行就重建硬编、退回「只有 MF_MT_AVG_BITRATE」，录制不中断，
    // 实际上限由 RateControlName() 如实上报（不能因为码控失败就判定无法录制）。
    std::string rate_control_error;
    if (!cbr_ && !vbr_) {
        rate_control_ = "avg_bitrate";
    } else if (!ApplyRateControl(rate_control_error)) {
        // 轮 47 F1：码控失败同样不许借回退把「点名硬编」悄悄换成软编。
        if (forced) {
            error = "encoder '" + requested_id_ + "' unavailable: MFT 码控协商失败（" +
                    rate_control_error + "）";
            Close();
            return false;
        }
        if (!hardware_) {
            rate_control_error_ = rate_control_error;
        } else {
            const std::string hardware_rate_error = rate_control_error;
            transform_.Reset();
            std::string software_error;
            if (CreateTransform(false, software_error) && Negotiate(software_error) &&
                ApplyRateControl(software_error)) {
                rate_control_error_.clear();
            } else {
                transform_.Reset();
                std::string rebuild_error;
                if (!CreateTransform(true, rebuild_error) || !Negotiate(rebuild_error)) {
                    error = "hardware rate control: " + hardware_rate_error +
                            "; software: " + software_error + "; rebuild: " + rebuild_error;
                    Close();
                    return false;
                }
                rate_control_error_ = hardware_rate_error;
            }
        }
    }

    // 标记为「已经开始接收数据」之前先发 START_OF_STREAM。
    transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    open_ = true;
    error.clear();
    return true;
}

bool MfVideoEncoder::Submit(const VideoFrame&) {
    // GPU 纹理直入需要厂商 SDK 或 MF DXGI 设备管理器，本阶段未实现。
    return false;
}

bool MfVideoEncoder::SubmitBytes(const uint8_t* data, size_t size, int64_t pts_100ns) {
    if (!open_ || transform_ == nullptr || data == nullptr || size == 0) {
        return false;
    }

    ComPtr<IMFSample> sample;
    if (FAILED(MFCreateSample(sample.GetAddressOf()))) {
        return false;
    }
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(size), buffer.GetAddressOf()))) {
        return false;
    }
    BYTE* destination = nullptr;
    if (FAILED(buffer->Lock(&destination, nullptr, nullptr))) {
        return false;
    }
    std::memcpy(destination, data, size);
    buffer->Unlock();
    buffer->SetCurrentLength(static_cast<DWORD>(size));
    sample->AddBuffer(buffer.Get());
    sample->SetSampleTime(pts_100ns);
    sample->SetSampleDuration(frame_duration_);

    HRESULT hr = transform_->ProcessInput(0, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING) {
        // 编码器缓冲区满：先抽干再重试一次。
        bool produced = false;
        std::string drain_error;
        while (DrainOne(produced, drain_error) && produced) {
        }
        hr = transform_->ProcessInput(0, sample.Get(), 0);
    }
    if (FAILED(hr)) {
        return false;
    }
    ++submitted_;
    return true;
}

bool MfVideoEncoder::StorePacket(IMFSample* sample) {
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(buffer.GetAddressOf()))) {
        return false;
    }
    BYTE* data = nullptr;
    DWORD length = 0;
    if (FAILED(buffer->Lock(&data, nullptr, &length))) {
        return false;
    }

    bool ok = false;
    if (length > 0) {
        std::vector<uint8_t> payload;
        if (AnnexBToAvcc(data, length, payload)) {
            packet_buffer_ = std::move(payload);
            ++annexb_converted_;
        } else {
            packet_buffer_.assign(data, data + length);
        }

        LONGLONG time = 0;
        sample->GetSampleTime(&time);
        PadToTargetBitrate();
        packet_view_.data = packet_buffer_.data();
        packet_view_.size = packet_buffer_.size();
        packet_view_.pts = Tick{time};
        packet_view_.dts = Tick{time};
        packet_view_.is_audio = false;
        packet_view_.keyframe = false;
        ++encoded_frames_;
        ok = true;
    }
    buffer->Unlock();
    return ok;
}

void MfVideoEncoder::PadToTargetBitrate() {
    if (!cbr_ || packet_buffer_.empty() || fps_ == 0) {
        return;
    }
    // 目标：每秒 bitrate_kbps_*1000 bit。按「本秒已输出到第几帧」给出线性预算，
    // 前面出得少就补在后面，前面超了（I 帧）就不补——这样每秒总量贴着标称值。
    //
    // 为什么用帧序号而不是 PTS：B 帧让 PTS 非单调（实测 11.033 → 11.099 → 11.066），
    // 按 PTS 选窗口会让「秒」来回切换、预算被反复清零并重复补齐；
    // 而录制循环每秒固定输出 fps 帧（静止桌面补重复帧），帧序号就是媒体时间。
    const int64_t window = static_cast<int64_t>(encoded_frames_) / static_cast<int64_t>(fps_);
    const int64_t index_in_window = static_cast<int64_t>(encoded_frames_) % static_cast<int64_t>(fps_);
    if (window != pad_window_) {
        pad_window_ = window;
        pad_bytes_in_window_ = 0;
    }
    const int64_t target_bytes_per_second = static_cast<int64_t>(bitrate_kbps_) * 1000 / 8;
    const int64_t budget = target_bytes_per_second * (index_in_window + 1) /
                           static_cast<int64_t>(fps_);
    const int64_t current = pad_bytes_in_window_ + static_cast<int64_t>(packet_buffer_.size());
    if (budget > current) {
        // AVCC 形式：4 字节长度 + NAL 头(0x0C = filler data) + 0xFF 填充。
        const int64_t total = budget - current;
        if (total >= 6) {
            // 长度前缀覆盖「NAL 头 + 载荷」= total - 4，而不是载荷本身：
            // 差 1 字节会让解析器把多出的 0xFF 当成下一个 NAL 的开头（mkv_probe 会判结构非法）。
            const size_t filler_payload = static_cast<size_t>(total) - 5;
            const size_t nal_length = filler_payload + 1;
            const size_t start = packet_buffer_.size();
            packet_buffer_.resize(start + static_cast<size_t>(total));
            packet_buffer_[start] = static_cast<uint8_t>((nal_length >> 24) & 0xFF);
            packet_buffer_[start + 1] = static_cast<uint8_t>((nal_length >> 16) & 0xFF);
            packet_buffer_[start + 2] = static_cast<uint8_t>((nal_length >> 8) & 0xFF);
            packet_buffer_[start + 3] = static_cast<uint8_t>(nal_length & 0xFF);
            packet_buffer_[start + 4] = 0x0C;
            std::memset(packet_buffer_.data() + start + 5, 0xFF, filler_payload);
            pad_bytes_in_window_ = current + total;
            filler_bytes_ += static_cast<uint64_t>(total);
            return;
        }
    }
    pad_bytes_in_window_ = current;
}

bool MfVideoEncoder::AnnexBToAvcc(const uint8_t* in, size_t size, std::vector<uint8_t>& out) {
    if (size < 5 || in[0] != 0 || in[1] != 0 || in[2] != 0 || in[3] != 1) {
        return false;  // 已经是 AVCC（或未知格式），保持原样
    }

    std::vector<size_t> starts;
    size_t index = 0;
    while (index + 4 <= size) {
        if (in[index] == 0 && in[index + 1] == 0 && in[index + 2] == 1) {
            starts.push_back(index);
            index += 3;
        } else if (index + 4 <= size && in[index] == 0 && in[index + 1] == 0 && in[index + 2] == 0 &&
                   in[index + 3] == 1) {
            starts.push_back(index);
            index += 4;
        } else {
            ++index;
        }
    }
    if (starts.empty()) {
        return false;
    }

    out.clear();
    out.reserve(size);
    for (size_t i = 0; i < starts.size(); ++i) {
        size_t payload_start = starts[i];
        if (in[payload_start + 2] == 1) {
            payload_start += 3;
        } else {
            payload_start += 4;
        }
        const size_t payload_end = (i + 1 < starts.size()) ? starts[i + 1] : size;
        if (payload_end <= payload_start) {
            continue;
        }
        const size_t payload_size = payload_end - payload_start;
        out.push_back(static_cast<uint8_t>((payload_size >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((payload_size >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((payload_size >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(payload_size & 0xFF));
        out.insert(out.end(), in + payload_start, in + payload_end);
    }
    return !out.empty();
}

bool MfVideoEncoder::DrainOne(bool& produced, std::string& error) {
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
    ComPtr<IMFSample> owned_sample;
    if ((stream_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0) {
        if (FAILED(MFCreateSample(owned_sample.GetAddressOf()))) {
            error = "MFCreateSample failed";
            return false;
        }
        ComPtr<IMFMediaBuffer> buffer;
        const DWORD capacity = stream_info.cbSize != 0 ? stream_info.cbSize : (1u << 20);
        if (FAILED(MFCreateMemoryBuffer(capacity, buffer.GetAddressOf()))) {
            error = "MFCreateMemoryBuffer failed";
            return false;
        }
        owned_sample->AddBuffer(buffer.Get());
        output.pSample = owned_sample.Get();
    }

    DWORD status = 0;
    const HRESULT hr = transform_->ProcessOutput(0, 1, &output, &status);

    if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
        // 编码器可能更换输出类型。重设时必须重新套用码率属性并重新下发码控，
        // 否则编码器会退回默认码率（实测表现为个别秒冲到标称的 2 倍以上）。
        ++stream_changes_;
        bool restored = false;
        for (DWORD index = 0;; ++index) {
            ComPtr<IMFMediaType> new_type;
            if (FAILED(transform_->GetOutputAvailableType(0, index, new_type.GetAddressOf()))) {
                break;
            }
            GUID subtype{};
            if (FAILED(new_type->GetGUID(MF_MT_SUBTYPE, &subtype)) || subtype != MFVideoFormat_H264) {
                continue;
            }
            if (ConfigureOutputType(new_type.Get())) {
                restored = true;
                break;
            }
        }
        if (restored && (cbr_ || vbr_)) {
            std::string rate_error;
            ApplyRateControl(rate_error);
        }
        stream_change_handled_ = true;
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
        if (StorePacket(output.pSample)) {
            produced = true;
        }
        if (owned_sample == nullptr) {
            output.pSample->Release();  // MFT 自带的样本由我们释放
        }
    }
    if (output.pEvents != nullptr) {
        output.pEvents->Release();
    }
    return true;
}

bool MfVideoEncoder::TryReceive(EncodedPacket& out) {
    bool produced = false;
    std::string error;
    if (!DrainOne(produced, error)) {
        return false;
    }
    if (!produced) {
        return false;
    }
    out = packet_view_;
    return true;
}

bool MfVideoEncoder::Flush(std::string& error) {
    if (!open_ || transform_ == nullptr) {
        error = "encoder is not open";
        return false;
    }
    flushing_ = true;
    transform_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
    error.clear();
    return true;
}

bool MfVideoEncoder::TryEncodeOneFrame(std::string& error) {
    if (!open_ || transform_ == nullptr) {
        error = "encoder is not open";
        return false;
    }
    const size_t frame_bytes = static_cast<size_t>(width_) * static_cast<size_t>(height_) * 3 / 2;
    if (frame_bytes == 0) {
        error = "invalid probe frame size";
        return false;
    }
    // 中性灰一帧 NV12（Y=0x80 / UV=0x80）：只验证 MFT 真能收帧并吐包，不测画质。
    const std::vector<uint8_t> nv12(frame_bytes, 0x80);
    // 带 B 帧的编码器会先缓冲：单帧拿不到包不等于不可用，故有界补到 4 帧再 DRAIN。
    for (uint32_t frame = 0; frame < 4; ++frame) {
        if (!SubmitBytes(nv12.data(), nv12.size(), static_cast<int64_t>(frame) * frame_duration_)) {
            error = "MFT ProcessInput 拒绝 NV12 帧";
            return false;
        }
        for (int attempt = 0; attempt < 8; ++attempt) {
            EncodedPacket packet;
            if (TryReceive(packet) && packet.size > 0) {
                return true;
            }
        }
    }
    std::string flush_error;
    if (!Flush(flush_error)) {
        error = "MFT DRAIN 失败（" + flush_error + "）";
        return false;
    }
    for (int attempt = 0; attempt < 16; ++attempt) {
        EncodedPacket packet;
        if (TryReceive(packet) && packet.size > 0) {
            return true;
        }
    }
    error = "MFT 试编码未产出 H.264 包（1~4 帧 + DRAIN）";
    return false;
}

bool MfVideoEncoder::ProbeAvailable(bool hardware, const EncoderProbeOptions& options,
                                    std::string& error) {
    // 与录制同一条路径：SetRequestedId 让协商/码控失败不回退，从而「列表说可用」
    // 与「--encoder <id> 真的用上它」由同一份实现保证，不会各写一遍口径。
    MfVideoEncoder probe;
    probe.SetRequestedId(hardware ? "mf-h264-hw" : "mf-h264-sw");
    probe.SetFailureInjection(options.fail_hardware, options.fail_software);

    // 代表性的录制参数（1280×720@30 / 6000kbps CBR）：与 --record 的默认档同量级。
    sr::VideoEncoderConfig config;
    config.width = 1280;
    config.height = 720;
    config.fps = 30;
    config.bitrate_kbps = 6000;
    config.max_bitrate_kbps = 12000;
    config.rate_control = sr::RateControlMode::Cbr;

    if (!probe.Open(config, error)) {
        error = "Open: " + error;
        return false;
    }
    if (!probe.TryEncodeOneFrame(error)) {
        error = "试编码一帧: " + error;
        return false;
    }
    error.clear();
    return true;
}

void MfVideoEncoder::Close() {
    if (transform_ != nullptr) {
        transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    }
    transform_.Reset();
    // 轮 47：启停按「本次 Open 是否真的启动过」配对，而不是按 open_。
    // 此前 Open 失败（探测常见路径）不关 MF、也不 CoUninitialize，重复探测会累积引用；
    // 现在失败分支同样释放，探测器可反复重试且不遗留 COM/MF 引用。
    if (mf_started_) {
        MFShutdown();
        mf_started_ = false;
    }
    if (com_owned_) {
        CoUninitialize();
        com_owned_ = false;
    }
    open_ = false;
    submitted_ = 0;
    encoded_frames_ = 0;
    flushing_ = false;
    pad_window_ = -1;
    pad_bytes_in_window_ = 0;
    // filler_bytes_ 不在 Close 清零：收尾后 stats 还要读它（Open 时清零）。
}

}  // namespace sr::encode
