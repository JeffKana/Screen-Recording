#include "encode/EncoderList.h"

#include "encode/MfVideoEncoder.h"

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace sr::encode {
namespace {

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

struct VendorSpec {
    const char* id;
    const char* name;
    std::vector<const wchar_t*> dlls;
    const char* vendor_hint;
};

// 厂商 SDK：一律不可用（本产品不引三方依赖，没集成厂商 SDK），
// 但原因来自真实探测（dll 是否存在），不是写死一句话。
const VendorSpec kVendors[] = {
    {"nvenc-h264", "NVIDIA NVENC H.264",
     {L"nvEncodeAPI64.dll", L"nvEncodeAPI.dll"}, "NVIDIA 驱动/SDK 未安装"},
    {"qsv-h264", "Intel oneVPL / QSV H.264",
     {L"libmfxhw64.dll", L"mfx_dispatch.dll", L"libmfx.dll"}, "Intel 显卡驱动/oneVPL 运行时未安装"},
    {"amf-h264", "AMD AMF H.264", {L"amf.dll"}, "AMD 驱动/AMF SDK 未安装"},
};

EncoderInfo MakeVendor(const VendorSpec& spec) {
    EncoderInfo info;
    info.id = spec.id;
    info.name = spec.name;
    info.hardware = true;
    info.available = false;
    std::string names;
    for (size_t i = 0; i < spec.dlls.size(); ++i) {
        if (i != 0) {
            names += "/";
        }
        names += Narrow(spec.dlls[i]);
    }
    std::string found;
    for (const wchar_t* dll : spec.dlls) {
        if (DllExists(dll)) {
            found = Narrow(dll);
            break;
        }
    }
    if (found.empty()) {
        info.reason = "未检测到 " + names + "（" + spec.vendor_hint + "）";
    } else {
        info.reason = "检测到 " + found + "，但本产品未集成厂商 SDK（不引三方依赖）";
    }
    return info;
}

// 轮 47 / M3-05#1：真激活探测。
// 不再用「MFTEnumEx + ActivateObject 成功」冒充可用——那只证明 COM 对象能创建，
// 不证明产品能协商/码控/编码。这里直接复用录制时同一个 MfVideoEncoder::Open，
// 并真的提交/取回一帧 H.264；列表与开录结果因此由**同一份实现**决定，不会分叉。
EncoderInfo MakeMf(bool hardware, const EncoderProbeOptions& options) {
    EncoderInfo info;
    info.id = hardware ? kHardwareId : kSoftwareId;
    info.name = hardware ? "MF 硬件 H.264 编码器" : "MF 软件 H.264 编码器";
    info.hardware = hardware;
    std::string error;
    info.available = MfVideoEncoder::ProbeAvailable(hardware, options, error);
    if (!info.available) {
        info.reason = "MFT 真激活失败（" + (error.empty() ? std::string("未知原因") : error) + "）";
    }
    return info;
}

}  // namespace

std::vector<EncoderInfo> ListEncoders(const EncoderProbeOptions& options) {
    std::vector<EncoderInfo> out;
    out.push_back(MakeMf(false, options));
    out.push_back(MakeMf(true, options));
    for (const VendorSpec& spec : kVendors) {
        out.push_back(MakeVendor(spec));
    }
    return out;
}

std::vector<EncoderInfo> ListEncoders() {
    return ListEncoders(EncoderProbeOptions{});
}

EncoderLookup LookupEncoder(const std::string& id) {
    EncoderLookup result;
    if (id == kSoftwareId || id == kHardwareId) {
        result.known = true;
        // MF 项不在这一层判可用：真正的可用性由 Open 的创建/协商/码控决定，
        // 与列表探测共用同一条路径（失败即 exit 6 + 非空原因）。
        result.available = true;
        result.hardware = (id == kHardwareId);
        return result;
    }
    for (const VendorSpec& spec : kVendors) {
        if (id == spec.id) {
            const EncoderInfo info = MakeVendor(spec);
            result.known = true;
            result.available = false;
            result.hardware = true;
            result.reason = info.reason;
            return result;
        }
    }
    return result;
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
