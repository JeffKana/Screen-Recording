#include "encode/EncoderList.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace sr::encode {
namespace {

using Microsoft::WRL::ComPtr;

constexpr const char* kSoftwareId = "mf-h264-sw";
constexpr const char* kHardwareId = "mf-h264-hw";

std::string Narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), bytes, nullptr, nullptr);
    return out;
}

std::string HexHr(long hr) {
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%08lx", static_cast<unsigned long>(hr) & 0xFFFFFFFFul);
    return buffer;
}

// 厂商 dll 的真实探测：只判「文件是否存在」，不 LoadLibrary（避免加载驱动带来的副作用）。
// 搜索范围：当前目录、%SystemRoot%\System32、PATH 的每一段——与 Windows 的 dll 搜索顺序一致。
bool DllExists(const wchar_t* name) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(fs::path(name), ec)) {
        return true;
    }
    wchar_t system[MAX_PATH] = {};
    if (GetSystemDirectoryW(system, MAX_PATH) > 0) {
        if (fs::exists(fs::path(system) / name, ec)) {
            return true;
        }
    }
    wchar_t path_buffer[32768] = {};
    const DWORD length = GetEnvironmentVariableW(L"PATH", path_buffer, 32768);
    if (length == 0 || length >= 32768) {
        return false;
    }
    std::wstring path(path_buffer, length);
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = path.find(L';', start);
        const std::wstring dir = path.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (!dir.empty() && fs::exists(fs::path(dir) / name, ec)) {
            return true;
        }
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

// 枚举某一类（硬件/软件）的 H.264 MFT，取**第一个能激活**的——与产品实际用的是同一个。
bool EnumerateFirstMft(bool hardware, EncoderInfo& info, std::string& error) {
    MFT_REGISTER_TYPE_INFO in_info{MFMediaType_Video, MFVideoFormat_NV12};
    MFT_REGISTER_TYPE_INFO out_info{MFMediaType_Video, MFVideoFormat_H264};
    const UINT32 flags = MFT_ENUM_FLAG_SORTANDFILTER |
                         (hardware ? MFT_ENUM_FLAG_HARDWARE : MFT_ENUM_FLAG_SYNCMFT);

    IMFActivate** activates = nullptr;
    UINT32 count = 0;
    const HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, flags, &in_info, &out_info,
                                 &activates, &count);
    if (FAILED(hr) || count == 0 || activates == nullptr) {
        error = hardware ? "MFTEnumEx 未枚举到硬件 H.264 MFT" : "MFTEnumEx 未枚举到软件 H.264 MFT";
        return false;
    }

    bool found = false;
    std::string last_error;
    // 只探测 activates[0]：产品实际用的就是它（MfVideoEncoder::CreateTransform 同一条路径），
    // 列表里的「可用/不可用」因此与产品行为一致，不会出现「列表说可用、产品打不开」。
    ComPtr<IMFTransform> transform;
    const HRESULT activate_hr = activates[0]->ActivateObject(
        __uuidof(IMFTransform), reinterpret_cast<void**>(transform.GetAddressOf()));
    std::string friendly;
    if (SUCCEEDED(activate_hr)) {
        // 友好名在 MFT 自己的属性上：必须走 IMFTransform::GetAttributes
        // （IMFTransform 不实现 IID_IMFAttributes，QueryInterface 取不到）。
        ComPtr<IMFAttributes> attributes;
        if (SUCCEEDED(transform->GetAttributes(attributes.GetAddressOf()))) {
            UINT32 length = 0;
            if (SUCCEEDED(attributes->GetStringLength(MFT_FRIENDLY_NAME_Attribute, &length)) && length > 0) {
                std::wstring buffer(length + 1, L'\0');
                UINT32 got = 0;
                if (SUCCEEDED(attributes->GetString(MFT_FRIENDLY_NAME_Attribute, buffer.data(),
                                                   length + 1, &got))) {
                    buffer.resize(got);
                    friendly = Narrow(buffer);
                }
            }
        }
    } else {
        last_error = HexHr(activate_hr);
    }
    for (UINT32 i = 0; i < count; ++i) {
        activates[i]->Release();
    }
    CoTaskMemFree(activates);

    if (SUCCEEDED(activate_hr)) {
        info.id = hardware ? kHardwareId : kSoftwareId;
        info.name = friendly.empty()
                        ? (hardware ? "MF 硬件 H.264 编码器" : "MF 软件 H.264 编码器")
                        : friendly;
        info.hardware = hardware;
        info.available = true;
        info.reason.clear();
        found = true;
    }

    if (!found) {
        info.id = hardware ? kHardwareId : kSoftwareId;
        info.name = hardware ? "MF 硬件 H.264 编码器" : "MF 软件 H.264 编码器";
        info.hardware = hardware;
        info.available = false;
        info.reason = "MFT 激活失败（hr=" + (last_error.empty() ? std::string("未知") : last_error) + "）";
        error = info.reason;
        return false;
    }
    return true;
}

// 厂商条目：**一律不可用**（本产品不引三方依赖，没集成厂商 SDK），
// 但原因来自真实探测（dll 是否存在），不是写死一句话。
void AppendVendor(const char* id, const char* name, bool hardware,
                  const std::vector<const wchar_t*>& dlls, const char* vendor_hint,
                  std::vector<EncoderInfo>& out) {
    EncoderInfo info;
    info.id = id;
    info.name = name;
    info.hardware = hardware;
    info.available = false;
    std::string names;
    for (size_t i = 0; i < dlls.size(); ++i) {
        if (i != 0) {
            names += "/";
        }
        names += Narrow(dlls[i]);
    }
    std::string found;
    for (const wchar_t* dll : dlls) {
        if (DllExists(dll)) {
            found = Narrow(dll);
            break;
        }
    }
    if (found.empty()) {
        info.reason = std::string("未检测到 ") + names + "（" + vendor_hint + "）";
    } else {
        info.reason = "检测到 " + found + "，但本产品未集成厂商 SDK（不引三方依赖）";
    }
    out.push_back(info);
}

}  // namespace

std::vector<EncoderInfo> ListEncoders() {
    std::vector<EncoderInfo> out;
    const HRESULT startup = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    (void)startup;

    // 1) MF 的 H.264 MFT（软编 / 硬编）：枚举 + 激活探测。
    EncoderInfo software;
    std::string error;
    if (!EnumerateFirstMft(false, software, error)) {
        out.push_back(software);
    } else {
        out.push_back(software);
    }

    EncoderInfo hardware;
    if (!EnumerateFirstMft(true, hardware, error)) {
        out.push_back(hardware);
    } else {
        out.push_back(hardware);
    }

    // 2) 厂商 SDK：没有 SDK 就列出来并说明原因（不消失）。
    AppendVendor("nvenc-h264", "NVIDIA NVENC H.264", true,
                 {L"nvEncodeAPI64.dll", L"nvEncodeAPI.dll"}, "NVIDIA 驱动/SDK 未安装", out);
    AppendVendor("qsv-h264", "Intel oneVPL / QSV H.264", true,
                 {L"libmfxhw64.dll", L"mfx_dispatch.dll", L"libmfx.dll"}, "Intel 显卡驱动/oneVPL 运行时未安装", out);
    AppendVendor("amf-h264", "AMD AMF H.264", true,
                 {L"amf.dll"}, "AMD 驱动/AMF SDK 未安装", out);

    MFShutdown();
    return out;
}

std::string FormatEncoderList(const std::vector<EncoderInfo>& encoders) {
    std::string out;
    for (const EncoderInfo& info : encoders) {
        if (!out.empty()) {
            out += ";";
        }
        out += "id=" + info.id + "|state=" + (info.available ? "available" : "unavailable");
        if (!info.available && !info.reason.empty()) {
            out += "|reason=" + info.reason;
        }
    }
    return out;
}

const EncoderInfo* FindEncoder(const std::vector<EncoderInfo>& encoders, const std::string& id) {
    for (const EncoderInfo& info : encoders) {
        if (info.id == id) {
            return &info;
        }
    }
    return nullptr;
}

}  // namespace sr::encode
